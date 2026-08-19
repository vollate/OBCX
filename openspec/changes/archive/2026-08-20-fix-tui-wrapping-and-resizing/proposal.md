## Why

The TUI renders each stored log entry as one terminal row, so text wider than the log pane is clipped instead of continuing on the next visual row. Its top/bottom split is also managed as a fixed row count initialized after the first render, which makes terminal resizing preserve stale dimensions and gives users an unreliable, poorly discoverable divider compared with tmux or Vim.

## What Changes

- Wrap log output to the current log-content width, including long unbroken text, while preserving log-level styling.
- Make scrolling, tail following, virtualization, and the log scrollbar operate on wrapped visual rows rather than only stored log entries.
- Keep the initial 70/30 log/CLI allocation, then preserve the user's chosen split proportion as the terminal height changes.
- Provide a visible, mouse-draggable horizontal divider and keyboard commands for resizing the two panes, with minimum usable heights and clamping for small terminals.
- Reflow displayed log and CLI output and recompute pane geometry immediately after terminal or divider changes.
- Add focused layout, wrapping, scrolling, and interaction tests.

## Capabilities

### New Capabilities

- `responsive-tui-layout`: Width-aware TUI text rendering, visual-row scrolling, and dynamically resizable log/CLI panes.

### Modified Capabilities

None.

## Impact

- `src/tui/tui_app.cpp` layout, rendering, scrolling, mouse routing, and resize handling.
- TUI state and testable layout/wrapping helpers under `include/tui/` and `src/tui/`.
- Core TUI unit/integration test registration under `tests/`.
- Existing FTXUI dependency is retained; no command API, configuration, database, or actor ABI changes are required.
