# Validation

The 0.1.0 package was built, installed, and exercised in an Ubuntu 26.04.1 LTS GNOME virtual machine. The checks below describe the tested scope; they do not establish compatibility with every desktop, distribution, file type, or application.

## Checked

- Release build with GCC 15.2.0, Meson 1.10.1, GTK 4.22.4, and Libadwaita 1.9.1; the final build completed without compiler warnings.
- Twelve core tests covering argument and path round-trips (including Unicode and shell metacharacters), working directories, add-without-run behavior, idempotent updates, scripts and interpreters, imported desktop settings, missing files, ownership checks, symlinks, and source-file preservation.
- Installed command-line flows for add, trial, enable, disable, update, remove, desktop import, and read-only inspection, using an isolated XDG configuration directory.
- GTK window startup in an Xvfb and D-Bus smoke run with GLib warnings treated as fatal.
- A real GNOME session flow covering native file selection, add-without-run, explicit trial, edit, disable, cancel and confirm delete, and source-file preservation.
- Login-session behavior: an enabled fixture ran once after login; after disabling it and logging out and back in, it did not run again.
- Debian package metadata, desktop entry syntax, dynamic-library dependencies, and installed package state.

## Not covered

- File-manager mouse drag-and-drop into the application window.
- Real application behavior for every document type and default handler.
- Every shebang/interpreter combination, missing terminal emulator, external concurrent configuration changes, and all application-specific outcomes.
- Dark theme, other screen sizes, other desktop environments, older Ubuntu releases, or other distributions.

The release build was not instrumented with AddressSanitizer, MemorySanitizer, or UndefinedBehaviorSanitizer. A successful launch does not prove that a target application completed its intended work.
