## 1. Build terminal-aware layout primitives

- [x] 1.1 Register a focused TUI layout test target and add failing cases for ASCII wrapping, preserved whitespace/newlines, long unbroken tokens, combining glyphs, and double-width glyphs
- [x] 1.2 Implement the pure UTF-8 terminal-cell wrapping helper with whitespace-preferred and glyph-safe hard-wrap behavior
- [x] 1.3 Add failing tests for first-frame 70/30 allocation, pane minimums, very small terminals, keyboard steps, and proportion preservation across height changes
- [x] 1.4 Implement the shared layout snapshot and ratio-aware split-state helpers used by rendering and event routing

## 2. Index wrapped log rows

- [x] 2.1 Add tests for monotonic log-entry identities and a consistent retained-range snapshot when the TUI sink appends and evicts entries
- [x] 2.2 Extend the bounded TUI sink with the sequence metadata and synchronized range operation required for incremental view updates without changing retention or formatting
- [x] 2.3 Add tests for wrapped-log cache initialization, append-only synchronization, front eviction, width invalidation, total visual-row counts, and visible range extraction
- [x] 2.4 Implement the incremental wrapped-log cache so stable-width refreshes wrap only new entries and width changes rebuild retained entries once
- [x] 2.5 Add and implement visual-row viewport/scrollbar tests for tail following, scrolled append stability, page bounds, reflow anchor restoration, and retained-anchor loss

## 3. Integrate responsive rendering and navigation

- [x] 3.1 Replace logical-line log virtualization with wrapped visual-row rendering while preserving level decoration, empty-state display, tail following, and bounded visible-row materialization
- [x] 3.2 Wrap CLI command-history output with the shared width policy while leaving FTXUI input editing and cursor behavior unchanged
- [x] 3.3 Route wheel, PageUp/PageDown, Home/End, scrollbar click, and scrollbar drag through visual-row counts and the current layout snapshot
- [x] 3.4 Initialize the FTXUI split before its first render, add a distinct draggable separator, observe mouse-selected sizes, and preserve the resulting proportion across terminal height changes
- [x] 3.5 Add `Ctrl+Up` and `Ctrl+Down` divider movement with shared minimum-height clamping and update pane/output geometry in the same render cycle
- [x] 3.6 Replace stale terminal-size and `mouse.y` assumptions in pane routing and scrollbar hit testing with the centralized layout snapshot

## 4. Verify behavior and regressions

- [x] 4.1 Add fixed-size FTXUI render/event tests for clipped-width regression, mouse divider dragging, keyboard resizing, terminal width reflow, terminal height ratio preservation, and minimum clamping
- [x] 4.2 Build the focused TUI test target and run its complete test set, including sanitizer coverage where supported
- [x] 4.3 Run the complete configured OBCX test suite and confirm existing CLI shutdown, log scrolling, and input-focus behavior remains intact
- [x] 4.4 Run `nix fmt` before every repository commit and verify formatting plus whitespace checks
- [x] 4.5 Run `openspec validate fix-tui-wrapping-and-resizing --strict --no-interactive`
