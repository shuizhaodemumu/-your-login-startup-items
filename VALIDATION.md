# Validation

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
