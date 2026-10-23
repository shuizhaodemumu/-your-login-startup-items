# Your Login Startup Items 0.2.4

Version 0.2.4 adds per-item window pinning and improves how the shared background helper is managed.

## New feature

- **Keep a selected window above ordinary windows.** Each startup item has an opt-in “Keep window above other windows” setting. The background helper reapplies the X11 EWMH `_NET_WM_STATE_ABOVE` state to matching windows.
- **X11 and XWayland support.** The feature can manage windows running on X11 or through XWayland. It does not require pinning to be enabled for every startup item.

## Improvements

- The background helper is started when either watchdog monitoring or window pinning needs it, and is removed after both features are disabled for all managed items.
- Fixed a helper lock handoff race that could occur when managed entries were switched rapidly, improving reliability when moving between helper-managed items.

## Compatibility and limits

- Window pinning is opt-in and defaults to off.
- Wayland-native windows cannot generally be controlled by another application through the standard cross-client interface. Fullscreen windows and desktop policy may also override the requested state.
- The feature uses EWMH and has been validated with automated tests and UI smoke checks; actual window placement under Mutter and GNOME login-session lifecycle were not covered by this validation.

## Validation

- Meson core, installed Debian application scan, and watchdog integration tests passed.
- GTK UI smoke test passed under Xvfb.
- Version `0.2.4` Debian package installed and its command-line version check passed in the Ubuntu 26.04 VM.

See [README.md](README.md) for installation and usage, and [VALIDATION.md](VALIDATION.md) for validation scope.
