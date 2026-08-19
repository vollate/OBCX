## Context

The fullscreen TUI is a vertical FTXUI `ResizableSplitTop` containing bordered log and command panes. The current log renderer turns every stored `LogLine` into one `ftxui::text` element and computes its virtual window from the number of stored lines. `text` does not wrap, so content beyond the pane width is clipped. Replacing it with `paragraph` alone is insufficient: long unbroken tokens still need hard wrapping, and the current scroll offsets, tail-follow logic, visible-row count, and scrollbar all assume one logical log entry equals one terminal row.

The split has a related state problem. FTXUI's `main_size` is an absolute row count. OBCX initializes that count to 70% only after calling `container->Render()` for the first time, never translates it when terminal height changes, and routes mouse events from the stored count rather than a single current layout model. FTXUI receives terminal resize events, but the OBCX pane and scrolling state therefore remain based on stale geometry. Although FTXUI provides a one-row draggable separator, the current presentation and surrounding event handling make it difficult to discover and do not offer a keyboard resize path.

The implementation must retain the 5,000-entry bounded sink, level colors, low-cost periodic refresh, tail following, command input, and existing scroll controls. It must handle UTF-8 terminal-cell widths and remain testable without running an interactive terminal loop.

## Goals / Non-Goals

**Goals:**

- Wrap log and command-history output to the current content width without losing text or splitting UTF-8 glyphs.
- Make log virtualization, scrolling, tail following, and scrollbar geometry use wrapped visual rows.
- Initialize a usable 70/30 split before the first render and preserve the user-selected proportion across terminal height changes.
- Support mouse and keyboard pane resizing with clear separator feedback and usable minimum pane heights.
- Reflow and clamp all dependent state whenever content width, pane height, or retained log data changes.
- Keep rendering bounded by cached layout work and the visible viewport.

**Non-Goals:**

- Replacing FTXUI or changing the CLI command model.
- Persisting pane proportions across process restarts.
- Adding horizontal scrolling or selectable/copyable log text.
- Parsing ANSI formatting beyond the sink's existing stripping behavior.
- Changing log retention, logger formatting, database state, or actor APIs.

## Decisions

### 1. Use terminal-cell-aware wrapping helpers rather than relying on `paragraph`

A small TUI layout module will expose pure helpers for wrapping UTF-8 text to a positive cell width. It will use FTXUI's public glyph and `string_width` utilities, preserve explicit line breaks and whitespace, prefer a whitespace boundary when available, and hard-wrap an overlong token at a glyph boundary. A glyph wider than the available width will occupy its own row rather than being split or discarded.

Each resulting segment is rendered as a separate `ftxui::text` element with the source log entry's level decorator. Command-history output uses the same wrapping policy; the editable `Input` remains owned by FTXUI so cursor and editing behavior are unchanged.

Using `ftxui::paragraph` was considered, but its flexbox word items do not guarantee splitting a URL, identifier, or other token wider than the pane, and it does not expose the stable visual-row mapping needed by OBCX scrolling. Byte slicing was rejected because it corrupts multibyte and double-width glyphs.

### 2. Maintain an incremental wrapped-log index

The sink will give retained log entries monotonic sequence identities and provide a consistent snapshot/range operation suitable for synchronizing a view cache. The TUI keeps an ordered cache of logical entries and their wrapped segments for the current content width.

At an unchanged width, synchronization removes evicted sequence identities and wraps only newly appended entries. A width change invalidates segment boundaries and rebuilds the retained cache once. The cache reports total visual rows, appended visual rows, and a requested visual-row range, allowing the renderer to materialize only the rows visible in the pane. The 5,000-entry retention bound remains unchanged.

Rebuilding every retained line on each 100 ms refresh was rejected because an earlier TUI optimization deliberately removed that cost. Wrapping only the currently visible logical lines was also rejected because the scrollbar and offsets require the total number of visual rows.

### 3. Express viewport state in visual rows and preserve a logical anchor during reflow

`log_scroll_offset_` continues to mean the number of rows below the viewport, but those rows are visual wrapped rows. At the tail it remains zero. When new entries arrive while the user is scrolled up, their appended visual-row count is added so the current content does not jump; retention eviction and bounds are then reconciled by sequence identity and clamping.

Before a width-driven rebuild, the cache records the top visible logical sequence and segment position. After wrapping, it restores the nearest row for that logical entry. If retention removed the anchor, it clamps to the nearest retained content. The scrollbar thumb and page increments are calculated from total and visible visual rows.

Keeping offsets in logical entries was rejected because one wheel step, page, and scrollbar location would vary unpredictably with line length. Resetting to the tail on every width change was rejected because it would disrupt users inspecting history.

### 4. Add a ratio-aware adapter around FTXUI's resizable split

A testable split-state helper will own the preferred log-pane proportion, current terminal height, minimum pane sizes, and the integer `main_size` passed to FTXUI. It initializes `main_size` before the first container render. Whenever terminal height changes, it derives a new row allocation from the stored proportion and clamps it so both bordered panes have at least one content row whenever the terminal is large enough.

FTXUI mouse dragging may update `main_size` directly. On the following render, the helper observes that change and updates the preferred proportion; subsequent terminal resizes preserve the user-selected proportion rather than the original 70/30 value. `Ctrl+Up` moves the divider up and `Ctrl+Down` moves it down one row, using the same clamp and proportion update path. The separator will use a visually distinct horizontal style.

Replacing `ResizableSplit` with a custom mouse-capture component was considered, but FTXUI already handles separator capture and drag correctly. Leaving `main_size` as an absolute count was rejected because it is the cause of stale pane allocation after terminal resizing.

### 5. Derive rendering and event geometry from one layout snapshot

Before building each frame, the app computes one layout snapshot from the current terminal dimensions and split state: bordered pane heights, log/console content widths and heights, separator row, and scrollbar bounds. Wrapping, visible-row calculation, mouse-pane routing, scrollbar hit testing, and keyboard clamping all consume that snapshot.

This removes the current fallback to a fresh `Terminal::Size()` in some paths and stale `log_pane_size` in others. Zero and very small dimensions are handled as best-effort layouts without negative sizes or invalid `std::clamp` ranges. A terminal resize therefore updates dependent state in the same render cycle.

### 6. Test pure layout behavior and FTXUI event integration separately

Unit tests will cover ASCII and UTF-8 wrapping, explicit newlines, long unbroken tokens, visual-row ranges, tail/anchor behavior, scrollbar calculations, initial allocation, minimum heights, and ratio preservation. A fixed-size FTXUI screen/component harness will render representative panes and synthesize divider mouse events plus `Ctrl+Up`/`Ctrl+Down` events. This avoids timing-sensitive tests around `ScreenInteractive::Loop` while still exercising the integration boundary.

## Risks / Trade-offs

- **[A width change requires rewrapping every retained entry]** → Keep retention bounded, rebuild only when width changes, and use incremental updates at a stable width.
- **[Unicode terminal width differs by terminal/font]** → Use the same FTXUI width functions used by rendering and test combining plus double-width glyphs.
- **[A terminal is too short for both normal minimums]** → Apply best-effort non-negative allocation and restore minimums automatically when enough rows return.
- **[Mouse drag and terminal resize occur in the same frame]** → Normalize both through the split-state helper, clamp once, and treat the final observed divider location as the user's preference.
- **[Reflow changes a scrolled viewport]** → Restore by logical sequence anchor and clamp only if the anchored entry was evicted.
- **[Additional cache state becomes inconsistent with the sink]** → Synchronize through one locked metadata/range operation and rebuild when sequence continuity cannot be proven.

## Migration Plan

1. Add and test the pure wrapping, viewport, and split-layout helpers.
2. Add monotonic retained-entry identity/snapshot support to the TUI sink and the wrapped-row cache.
3. Integrate visual-row rendering and scrolling, then integrate ratio-aware mouse and keyboard resizing.
4. Run focused TUI tests, the complete test suite, `nix fmt`, and strict OpenSpec validation.

No data migration or configuration change is required. Rollback restores the previous TUI renderer; log storage and CLI behavior remain compatible.

## Open Questions

None. Pane-size persistence and richer text selection can be proposed separately if needed.
