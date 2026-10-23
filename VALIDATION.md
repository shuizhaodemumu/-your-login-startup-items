# Validation

## Version 0.2.4

Built on Ubuntu 26.04.1 LTS amd64 with GCC 15.2.0, Meson 1.10.1, GTK 4.22.4, Libadwaita 1.9.1, and X11 development libraries. Added an independent per-item option that keeps identifiable X11/XWayland windows above ordinary windows through the existing background helper. The helper's login entry is now maintained for watchdog or window-pinning use and removed after both are disabled.

- The unit suite covers keep-above configuration persistence and creation/removal of the helper login entry for pin-only use. The watchdog integration suite verifies that a pin-only item keeps the background helper alive after enable and that disabling it removes the helper entry and lets the daemon exit; it also covers helper handoff during rapid changes between managed items.
- All three Meson tests passed; the watchdog integration ran successfully. GTK4/Libadwaita startup passed under Xvfb with fatal application warnings enabled.
- Debian package `login-startup_0.2.4_amd64.deb` was assembled and installed in the VM.
- Actual EWMH interaction against a real managed X11 window and real GNOME login-session behavior were not exercised automatically. Wayland-native windows are outside generic cross-client control; fullscreen and compositor policy may supersede the request.
- Gemini selected and executed the core and UI checks; both exited 0. Its report confirms core and basic UI startup passed and marks actual EWMH stacking and GNOME login lifecycle as untested. The raw plan and logs are in `D:/codex create/Codex-Gemini协作/reports/login-startup-ubuntu-2026-10-23T05-35-09-722Z-f55c16f7-5a0c-4305-b892-bed4329e572c/`.
- After the lock-handoff integration was added, Gemini also selected and ran the updated watchdog integration test; it exited 0. Its report generation is still pending. Plan and logs are in `D:/codex create/Codex-Gemini协作/reports/login-startup-ubuntu-2026-10-23T05-47-45-475Z-57483a39-8578-412d-8981-97a226ce5107/`.
- The installed binary reports version 0.2.4. An actual EWMH stacking test remains uncovered because this VM lacks a window manager under the automated Xvfb session.

## Version 0.2.3

Built and installed in the Ubuntu 26.04.1 LTS amd64 VM with GCC 15.2.0, Meson 1.10.1, GTK 4.22.4, and Libadwaita 1.9.1. `login-startup --version` reports 0.2.3.

- All three Meson tests pass. Core coverage includes delay serialization and the 3600-second bound, silent-mode terminal/startup-notification suppression, restoring original `.desktop` values when silent mode is turned off, and preserving absent settings.
- GTK4/Libadwaita startup passed under Xvfb with fatal application warnings enabled.
- Startup delay is stored in GNOME's autostart entry in seconds. Real GNOME login-session timing was not tested in this validation run.
- Silent mode suppresses the terminal window and GNOME startup notification; it cannot hide a target application's own window or dialogs.
- Package `login-startup_0.2.3_amd64.deb` was assembled and installed in the VM.
- Gemini independent review could not run: the local Antigravity CLI model check failed before selecting tests. The Codex-run build, tests, and GUI smoke check completed successfully.

## Version 0.2.2

Built and installed in the Ubuntu 26.04.1 LTS amd64 VM with GCC 15.2.0, Meson 1.10.1, GTK 4.22.4, and Libadwaita 1.9.1. The installed package and `login-startup --version` report 0.2.2.

- All three Meson tests pass. The watchdog integration test now covers executable `.run` launchers that copy themselves to a stable path, contain binary data in their header buffer, and have trailing bytes beyond the embedded payload; it verifies existing-instance detection, restart after exit, and no relaunch after disabling.
- Tested against `/home/kkak/音乐/Content.run`: the pre-existing process using its copied `EncryptAssetsUbuntu.run` was recognized without a duplicate launch. No decryption was required; the embedded payload hash matched.
- Debian package assembly completed and the 0.2.2 package was installed in the VM. The real test startup item and test-spawned process were removed afterward; the user's pre-existing application process was left running.

## Version 0.2.1

Version 0.2.1 was built and installed in an Ubuntu 26.04.1 LTS amd64 VM using GCC 15.2.0, Meson 1.10.1, GTK 4.22.4, and Libadwaita 1.9.1. `dpkg-query` and `login-startup --version` both reported 0.2.1.

## Checked

- Release build completed without compiler warnings; all three Meson tests passed: core behavior, installed deb GUI app discovery, and watchdog integration.
- Core tests cover launch configuration, scripts and desktop files, path and argument handling, shortcut copy safety, watchdog eligibility and self-start configuration, duplicate targets, and preservation of source files.
- The installed-app scan test ran against the VM's dpkg-owned launchers; a manual CLI scan listed 31 visible installed GUI applications.
- The watchdog integration test verified restart behavior for a directly launched executable and a simple `.desktop` launcher, then confirmed that disabling the watchdog stops relaunches.
- GTK4/Libadwaita window startup passed under Xvfb with fatal warnings enabled.
- Debian package assembly completed, `desktop-file-validate` passed, and `dpkg-deb -I` confirmed package `login-startup` version `0.2.1`, architecture `amd64`, and generated runtime dependencies.
- The installed-app picker defaults to zero selections; the UI explains that users choose the apps they want.

## Not covered

- Creating shortcuts through actual UI clicks, search and multi-select interaction, or testing on multiple desktop environments.
- Real GNOME login-session testing of the new watchdog helper's autostart lifecycle.
- Timing verification of every watchdog backoff interval, prolonged retry behavior, or all unsupported launcher types.
- Compatibility outside Ubuntu 26.04 LTS amd64, dark theme, accessibility review, or sanitizers.

The watchdog supports direct executables, identifiable `.desktop` launchers, and executable `.run` launchers. Other shell/script wrappers remain unsupported where process identity cannot be tracked reliably. Closing a watched app manually will relaunch it until the watchdog is disabled.
