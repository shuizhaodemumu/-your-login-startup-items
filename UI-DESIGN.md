# UI design

The interface uses a light background, a restrained teal accent, a clear file selection area, and separate cards for managed startup items. Native GTK controls keep labels, buttons, and switches accessible and interactive. Generated concept art is used only as a visual reference; it is not shown as a clickable screenshot or used in place of real controls.

The installed-app picker keeps the existing manager uncluttered: one button opens a searchable, multi-select list with application icons and package names. Users can create desktop shortcut copies in one batch. Advanced settings provide independent watchdog and window-pinning choices. The summary shows whether the helper is configured for login and whether it is currently running. These are separate states so enabling an item does not imply the helper is currently active.

The first release's interface text is primarily Simplified Chinese. The project documentation is available in English and Simplified Chinese.

Advanced startup settings include a GNOME login delay in seconds and a silent-launch toggle. Silent launch suppresses terminal windows and GNOME startup notification while keeping the limitation clear that the target application's own windows and dialogs remain under that application’s control.
