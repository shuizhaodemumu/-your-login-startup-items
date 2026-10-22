# Your Login Startup Items

A native GTK4 desktop app for managing files that start automatically when you sign in to your GNOME desktop. The first release targets Ubuntu 26.04 LTS on amd64.

[简体中文](README.zh-CN.md)

## Features

- Add local programs, scripts, documents, and existing `.desktop` launchers to your per-user login startup list.
- Enable, disable, edit, remove, and explicitly trial-run managed items.
- Set a working directory, arguments, and terminal preference.
- Preserve imported desktop launcher settings, including its original command, icon, terminal choice, and desktop restrictions.
- Scan installed deb packages for visible graphical applications, then search, select, and create desktop shortcuts in batches.
- Enable a watchdog for supported programs or launchers. A small background helper starts at login and relaunches watched processes after they exit, using exponential backoff from 2 seconds up to 60 seconds.
- Adding an item only reads its configuration and saves a launcher; it never runs the selected file.
- Manage only entries owned by this application. Removing an entry does not delete its source file.

The application scan covers visible launchers in `/usr/share/applications` that belong to an installed deb package. Libraries, command-line tools, and packages without a desktop launcher are not included. Watchdogs support directly executable programs and `.desktop` launchers whose command can be matched to a process; scripts, shell/`env` wrappers, and launchers whose process cannot be identified are excluded. Manually closing a watched program also triggers a restart. Turn off its watchdog to stop that behavior.

## Requirements

- Ubuntu 26.04 LTS with GNOME (amd64)
- C compiler, Meson, Ninja, GTK4, Libadwaita, GLib, and Debian packaging tools to build
- `xdg-terminal-exec` for terminal trials

## Build a Debian package

```sh
sudo apt install build-essential meson ninja-build libgtk-4-dev libadwaita-1-dev desktop-file-utils dpkg-dev
sh packaging/build-deb.sh
sudo apt install ./dist/login-startup_0.2.1_amd64.deb
```

Run `login-startup` or open **Your Login Startup Items** from the Applications menu. The package targets Ubuntu 26.04 LTS; binaries are not promised to work on older releases.

## Command-line examples

```sh
login-startup --add /path/to/program
login-startup --list
login-startup --try ITEM_ID
login-startup --disable ITEM_ID
login-startup --enable ITEM_ID
login-startup --remove ITEM_ID
login-startup --inspect /path/to/file
login-startup --list-installed-apps
login-startup --watchdog-enable ITEM_ID
login-startup --watchdog-disable ITEM_ID
```

`--inspect` only checks the selected file. It does not save or run it. Arguments are passed without shell expansion.

## Data and safety

Managed entries are stored in `~/.config/autostart/`. When a watchdog is enabled, the app writes a separate per-user login entry for its background helper. It removes that entry after the last active watchdog is disabled. The helper identifies processes through `/proc`; it retries indefinitely with delays capped at 60 seconds. Remove managed entries before uninstalling the package. Imported `.desktop` launchers retain their own launch command.

Created desktop shortcuts are executable copies of the original `.desktop` files in the configured user Desktop directory. Removing a shortcut does not uninstall its application.

The app does not follow symbolic links when managing entries. Files moved after setup are not tracked automatically. Script execution still relies on the script's interpreter; the app cannot determine whether a script is trustworthy.

## Development

The application is written in C with GTK4, Libadwaita, and GLib. Meson builds the application and its core test executable. Tests use isolated user configuration directories where applicable.

See [VALIDATION.md](VALIDATION.md) for the scope and limits of the first release's validation.

## License

This project is available under the MIT License. See [LICENSE](LICENSE).
