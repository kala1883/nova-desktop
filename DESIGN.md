# NOVA Desktop — incumbent UI refinement

Native Win32 desktop launcher; an Operate surface. Preserve the existing dark desktop identity while prioritizing scanability, familiar Windows interaction and low idle cost.

- Background `#0e131d`; sidebar / controls `#161d2a`; primary text `#ecf1f9`; secondary text `#a7b5ca`; primary action `#354d7a`.
- Microsoft YaHei UI body at 15 logical px; 12 px status; 32 px workspace title. Segoe UI wordmark at 29 px.
- 220 logical px sidebar. A drawn plus button beside the workspace heading creates a workspace. Top-right drawn pin and gear buttons provide always-on-top and settings. Settings contains startup, rename, delete and exit. Content begins at 254 px.
- File identity uses Shell icons. Native list view handles selection, scrolling, keyboard navigation, tooltips and empty state. No permanent decorative animation or full-screen back buffer.
- Double click or Enter launches; drag-in only imports. Feedback reports added/skipped counts; destructive workspace actions require confirmation.
- System-DPI-aware scaling. Minimum window 780 × 600 logical px; launch maximized. Native focus indication is retained.

Pinning changes only the HWND_TOPMOST band, preserving window geometry and resizing. Icon buttons have accessible names, tooltips, keyboard focus, hover feedback and a persistent highlighted pin state.

Caption controls share a single 30 logical px title bar: pin, settings, minimize, maximize/restore, close, aligned flush right in 46 px cells. Use 1 logical px strokes, neutral dark hover, red close hover; leave the workspace header free of window controls. Caption blank space supports native drag and double-click maximize through hit testing.

Validation: inspect the rendered Windows surface and keyboard/control names; core import/persistence/startup tests use isolated state, and window integration verifies topmost on/off without changing geometry.
