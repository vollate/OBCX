#include "tui/tui_app.hpp"
#include "common/cli_handler.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace obcx::common {

namespace {

auto level_color(spdlog::level::level_enum level) -> ftxui::Color {
  switch (level) {
  case spdlog::level::trace:
    return ftxui::Color::GrayDark;
  case spdlog::level::debug:
    return ftxui::Color::Cyan;
  case spdlog::level::info:
    return ftxui::Color::Green;
  case spdlog::level::warn:
    return ftxui::Color::Yellow;
  case spdlog::level::err:
    return ftxui::Color::Red;
  case spdlog::level::critical:
    return ftxui::Color::Red;
  default:
    return ftxui::Color::White;
  }
}

auto level_decorator(spdlog::level::level_enum level) -> ftxui::Decorator {
  if (level == spdlog::level::critical) {
    return ftxui::color(ftxui::Color::Red) | ftxui::bold;
  }
  return ftxui::color(level_color(level));
}

auto compute_log_visible_rows(int log_pane_size) -> int {
  if (log_pane_size > 2) {
    return std::max(log_pane_size - 2, 1);
  }
  return std::max(ftxui::Terminal::Size().dimy * 7 / 10 - 2, 1);
}

auto log_max_offset(int total, int visible_rows) -> int {
  return std::max(0, total - visible_rows);
}

} // namespace

TuiApp::TuiApp(std::shared_ptr<tui_sink_mt> tui_sink,
               CliHandler::Context cli_ctx)
    : tui_sink_(std::move(tui_sink)), cli_ctx_(std::move(cli_ctx)) {}

void TuiApp::run() {
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  screen.ForceHandleCtrlC(false);

  std::string input_text;
  std::mutex console_mutex;

  cli_ctx_.output_cb = [this, &console_mutex](const std::string &msg) -> void {
    std::scoped_lock lock(console_mutex);
    console_lines_.push_back(msg);
  };

  CliHandler cli_handler(cli_ctx_);

  auto input_component = ftxui::Input(&input_text, "> ");

  auto input_with_enter = ftxui::CatchEvent(
      input_component, [&](const ftxui::Event &event) -> bool {
        if (event == ftxui::Event::Return) {
          std::string cmd = input_text;
          input_text.clear();

          if (!cmd.empty()) {
            {
              std::scoped_lock lock(console_mutex);
              console_lines_.push_back("> " + cmd);
            }
            if (!cli_handler.process_command(cmd)) {
              // Don't exit immediately — signal shutdown and let it complete
              cli_ctx_.should_stop.store(true, std::memory_order_release);
              cli_ctx_.stop_cv.notify_one();
            }
          }
          return true;
        }
        if (event == ftxui::Event::CtrlC) {
          {
            std::scoped_lock lock(console_mutex);
            console_lines_.emplace_back("Exiting...");
          }
          cli_handler.process_command("exit");
          cli_ctx_.should_stop.store(true, std::memory_order_release);
          cli_ctx_.stop_cv.notify_one();
          return true;
        }
        return false;
      });

  // Background thread to refresh the TUI periodically for new log lines
  // Also monitors should_stop and runs shutdown callback before exiting
  std::thread refresh_thread([&screen, this]() -> void {
    while (running_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      if (cli_ctx_.should_stop.load(std::memory_order_acquire)) {
        bool expected = false;
        if (shutdown_started_.compare_exchange_strong(expected, true)) {
          // Run shutdown callback in a separate thread so TUI keeps refreshing
          if (cli_ctx_.shutdown_cb) {
            std::thread shutdown_thread([this]() -> void {
              cli_ctx_.shutdown_cb();
              shutdown_complete_.store(true, std::memory_order_release);
            });
            shutdown_thread.detach();
          } else {
            shutdown_complete_.store(true, std::memory_order_release);
          }
        }
      }

      if (shutdown_complete_.load(std::memory_order_acquire)) {
        // Give one last refresh so final log lines appear
        screen.PostEvent(ftxui::Event::Custom);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        running_ = false;
        screen.Exit();
        return;
      }

      screen.PostEvent(ftxui::Event::Custom);
    }
  });

  int log_pane_size = 0; // populated by ResizableSplit on first draw

  // Virtualized log pane: only renders rows currently on-screen.
  auto log_pane = ftxui::Renderer([this, &log_pane_size] -> ftxui::Element {
    auto total = static_cast<int>(tui_sink_->line_count());
    auto version = tui_sink_->version();

    if (total == 0) {
      log_scroll_offset_ = 0;
      log_follow_tail_ = true;
      last_log_version_ = version;
      log_visible_rows_ = compute_log_visible_rows(log_pane_size);
      log_scrollbar_track_height_ = log_visible_rows_;
      log_scrollbar_thumb_top_ = 0;
      log_scrollbar_thumb_height_ = log_scrollbar_track_height_;
      log_scrollbar_x_ = ftxui::Terminal::Size().dimx - 2;
      return ftxui::text("(no log output yet)") | ftxui::dim |
             ftxui::yflex_grow;
    }

    log_visible_rows_ = compute_log_visible_rows(log_pane_size);
    int visible_rows = log_visible_rows_;
    int max_offset = log_max_offset(total, visible_rows);

    if (version < last_log_version_) {
      last_log_version_ = version;
    }

    if (version > last_log_version_) {
      auto appended = static_cast<int>(std::min<uint64_t>(
          version - last_log_version_, static_cast<uint64_t>(max_offset)));
      if (log_follow_tail_) {
        log_scroll_offset_ = 0;
      } else {
        log_scroll_offset_ += appended;
      }
      last_log_version_ = version;
    }

    log_scroll_offset_ = std::clamp(log_scroll_offset_, 0, max_offset);
    log_follow_tail_ = log_scroll_offset_ == 0;

    // Compute the visible window. offset=0 means viewing the latest line at
    // the bottom; positive offsets scroll up into history.
    int end_idx = total - log_scroll_offset_;
    int start_idx = std::max(0, end_idx - visible_rows);

    auto lines = tui_sink_->get_lines_range(
        static_cast<std::size_t>(start_idx),
        static_cast<std::size_t>(end_idx - start_idx));

    ftxui::Elements log_elements;
    log_elements.reserve(lines.size());
    for (const auto &line : lines) {
      log_elements.push_back(ftxui::text(line.stripped_text) |
                             level_decorator(line.level));
    }
    while (static_cast<int>(log_elements.size()) < visible_rows) {
      log_elements.push_back(ftxui::text(""));
    }

    log_scrollbar_track_height_ = visible_rows;
    if (max_offset == 0) {
      log_scrollbar_thumb_height_ = visible_rows;
      log_scrollbar_thumb_top_ = 0;
    } else {
      log_scrollbar_thumb_height_ =
          std::clamp((visible_rows * visible_rows) / total, 1, visible_rows);
      int movable = std::max(1, visible_rows - log_scrollbar_thumb_height_);
      float progress_from_top = 1.f - static_cast<float>(log_scroll_offset_) /
                                          static_cast<float>(max_offset);
      log_scrollbar_thumb_top_ =
          std::clamp(static_cast<int>(std::lround(progress_from_top * movable)),
                     0, movable);
    }
    log_scrollbar_x_ = std::max(0, ftxui::Terminal::Size().dimx - 2);

    ftxui::Elements scrollbar;
    scrollbar.reserve(static_cast<std::size_t>(visible_rows));
    for (int row = 0; row < visible_rows; ++row) {
      bool in_thumb =
          row >= log_scrollbar_thumb_top_ &&
          row < log_scrollbar_thumb_top_ + log_scrollbar_thumb_height_;
      scrollbar.push_back(ftxui::text(" ") |
                          ftxui::bgcolor(in_thumb ? ftxui::Color::GrayLight
                                                  : ftxui::Color::GrayDark));
    }

    return ftxui::hbox({ftxui::vbox(std::move(log_elements)) |
                            ftxui::yflex_grow | ftxui::xflex,
                        ftxui::vbox(std::move(scrollbar)) |
                            ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1)}) |
           ftxui::yflex_grow;
  });

  // Console pane: pure renderer, delegates to input_with_enter
  auto console_pane = ftxui::Renderer(input_with_enter, [&] -> ftxui::Element {
    ftxui::Elements console_elements;
    {
      std::scoped_lock lock(console_mutex);
      console_elements.reserve(console_lines_.size());
      for (const auto &line : console_lines_) {
        console_elements.push_back(ftxui::text(line));
      }
    }
    console_elements.push_back(input_with_enter->Render());

    auto total = static_cast<int>(console_elements.size());
    console_scroll_offset_ =
        std::clamp(console_scroll_offset_, 0, std::max(0, total - 1));

    float position = total > 1
                         ? 1.f - static_cast<float>(console_scroll_offset_) /
                                     static_cast<float>(total - 1)
                         : 1.f;
    position = std::clamp(position, 0.f, 1.f);

    return ftxui::vbox(std::move(console_elements)) |
           ftxui::focusPositionRelative(0.f, position) |
           ftxui::vscroll_indicator | ftxui::yframe | ftxui::yflex_grow;
  });

  auto log_bordered = ftxui::Renderer(log_pane, [&log_pane] -> ftxui::Element {
    return log_pane->Render() | ftxui::borderStyled(ftxui::Color::Blue) |
           ftxui::bold;
  });

  auto console_bordered =
      ftxui::Renderer(console_pane, [&console_pane] -> ftxui::Element {
        return console_pane->Render() |
               ftxui::borderStyled(ftxui::Color::Green);
      });

  auto container =
      ftxui::ResizableSplitTop(log_bordered, console_bordered, &log_pane_size);

  // Set initial split ratio (70/30)
  bool size_initialized = false;

  auto inner = ftxui::Renderer(container, [&] -> ftxui::Element {
    auto element = container->Render();
    if (!size_initialized) {
      log_pane_size = ftxui::Terminal::Size().dimy * 7 / 10;
      size_initialized = true;
    }
    return element;
  });

  // Root: handle all mouse events here using mouse.y vs log_pane_size
  // to route scroll and click events to the correct pane.
  auto root = ftxui::CatchEvent(inner, [&](ftxui::Event event) -> bool {
    auto scroll_log_to = [this](int offset) -> void {
      auto total = static_cast<int>(tui_sink_->line_count());
      auto max_offset = log_max_offset(total, log_visible_rows_);
      log_scroll_offset_ = std::clamp(offset, 0, max_offset);
      log_follow_tail_ = log_scroll_offset_ == 0;
    };

    auto scroll_log_by = [&](int delta) -> void {
      scroll_log_to(log_scroll_offset_ + delta);
    };

    auto log_offset_from_thumb_top = [this](int thumb_top) -> int {
      auto total = static_cast<int>(tui_sink_->line_count());
      auto max_offset = log_max_offset(total, log_visible_rows_);
      int movable = log_scrollbar_track_height_ - log_scrollbar_thumb_height_;
      if (max_offset <= 0 || movable <= 0) {
        return 0;
      }
      thumb_top = std::clamp(thumb_top, 0, movable);
      float progress_from_top =
          static_cast<float>(thumb_top) / static_cast<float>(movable);
      return static_cast<int>(
          std::lround((1.f - progress_from_top) * max_offset));
    };

    if (!event.is_mouse()) {
      if (event == ftxui::Event::PageUp) {
        scroll_log_by(std::max(1, log_visible_rows_ - 1));
        return true;
      }
      if (event == ftxui::Event::PageDown) {
        scroll_log_by(-std::max(1, log_visible_rows_ - 1));
        return true;
      }
      if (event == ftxui::Event::Home) {
        auto total = static_cast<int>(tui_sink_->line_count());
        scroll_log_to(log_max_offset(total, log_visible_rows_));
        return true;
      }
      if (event == ftxui::Event::End) {
        scroll_log_to(0);
        return true;
      }
      return false;
    }

    auto &mouse = event.mouse();
    bool in_log_pane = mouse.y < log_pane_size;
    bool on_log_scrollbar =
        in_log_pane && mouse.x >= std::max(0, log_scrollbar_x_ - 1) &&
        mouse.y >= 1 && mouse.y < 1 + log_scrollbar_track_height_;

    if (log_scrollbar_dragging_) {
      if (mouse.button == ftxui::Mouse::Left &&
          mouse.motion == ftxui::Mouse::Moved) {
        int delta_y = mouse.y - log_scrollbar_drag_start_y_;
        scroll_log_to(log_offset_from_thumb_top(
            log_scrollbar_drag_start_thumb_top_ + delta_y));
        return true;
      }
      if (mouse.button == ftxui::Mouse::Left &&
          mouse.motion == ftxui::Mouse::Released) {
        log_scrollbar_dragging_ = false;
        return true;
      }
    }

    if (mouse.button == ftxui::Mouse::WheelUp) {
      if (in_log_pane) {
        scroll_log_by(3);
      } else {
        console_scroll_offset_ += 3;
      }
      return true;
    }
    if (mouse.button == ftxui::Mouse::WheelDown) {
      if (in_log_pane) {
        scroll_log_by(-3);
      } else {
        console_scroll_offset_ -= 3;
        if (console_scroll_offset_ < 0) {
          console_scroll_offset_ = 0;
        }
      }
      return true;
    }

    if (on_log_scrollbar && mouse.button == ftxui::Mouse::Left &&
        mouse.motion == ftxui::Mouse::Pressed) {
      int track_y = std::clamp(mouse.y - 1, 0,
                               std::max(0, log_scrollbar_track_height_ - 1));
      if (track_y >= log_scrollbar_thumb_top_ &&
          track_y < log_scrollbar_thumb_top_ + log_scrollbar_thumb_height_) {
        log_scrollbar_drag_start_y_ = mouse.y;
        log_scrollbar_drag_start_thumb_top_ = log_scrollbar_thumb_top_;
      } else {
        int movable = std::max(0, log_scrollbar_track_height_ -
                                      log_scrollbar_thumb_height_);
        int thumb_top =
            std::clamp(track_y - log_scrollbar_thumb_height_ / 2, 0, movable);
        scroll_log_to(log_offset_from_thumb_top(thumb_top));
        log_scrollbar_drag_start_y_ = mouse.y;
        log_scrollbar_drag_start_thumb_top_ = thumb_top;
      }
      log_scrollbar_dragging_ = true;
      return true;
    }

    if (!in_log_pane && mouse.button == ftxui::Mouse::Left &&
        mouse.motion == ftxui::Mouse::Pressed) {
      input_component->TakeFocus();
      return false; // let the event propagate to the input component
    }

    return false;
  });

  screen.Loop(root);

  cli_ctx_.should_stop.store(true, std::memory_order_release);
  cli_ctx_.stop_cv.notify_one();

  running_ = false;
  if (refresh_thread.joinable()) {
    refresh_thread.join();
  }

  // If shutdown didn't start (e.g., SIGINT interrupted screen.Loop()
  // before the refresh thread could start the callback), run it now.
  if (cli_ctx_.shutdown_cb) {
    bool expected = false;
    if (shutdown_started_.compare_exchange_strong(expected, true)) {
      cli_ctx_.shutdown_cb();
    } else {
      // Shutdown was started by the refresh thread but may not be done yet.
      // Wait for it to complete.
      while (!shutdown_complete_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }
  }
}

} // namespace obcx::common
