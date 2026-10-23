#pragma once
#include <gio/gdesktopappinfo.h>

#define LS_GROUP "Desktop Entry"
#define LS_PREFIX "login-startup-"
#define LS_OWNER "X-LoginStartup-Managed"
#define LS_SOURCE "X-LoginStartup-Source"
#define LS_KIND "X-LoginStartup-Kind"
#define LS_ARGS "X-LoginStartup-Arguments"
#define LS_ID "X-LoginStartup-Id"
#define LS_WATCHDOG "X-LoginStartup-Watchdog"
#define LS_KEEP_ABOVE "X-LoginStartup-KeepAbove"
#define LS_SILENT "X-LoginStartup-Silent"
#define LS_DELAY "X-GNOME-Autostart-Delay"
#define LS_SILENT_TERMINAL_PRESENT "X-LoginStartup-Silent-Terminal-Present"
#define LS_SILENT_TERMINAL_VALUE "X-LoginStartup-Silent-Terminal-Value"
#define LS_SILENT_NOTIFY_PRESENT "X-LoginStartup-Silent-Notify-Present"
#define LS_SILENT_NOTIFY_VALUE "X-LoginStartup-Silent-Notify-Value"
#define LS_WATCHDOG_AUTOSTART "login-startup-watchdog.desktop"

typedef struct { char *path; GKeyFile *key; } LsItem;
typedef struct {
    char *path;
    char *desktop_id;
    char *name;
    char *package;
    GIcon *icon;
    gboolean shortcut_exists;
} LsInstalledApp;
char *ls_directory(void);
char *ls_id_for_source(const char *source);
gboolean ls_valid_id(const char *id);
GKeyFile *ls_draft(const char *source, const char *name, const char *args,
                  const char *cwd, gboolean terminal, GError **error);
gboolean ls_validate(GKeyFile *key, GError **error);
gboolean ls_save(GKeyFile *key, GError **error);
gboolean ls_set_startup_options(GKeyFile *key, guint delay_seconds, gboolean silent, GError **error);
void ls_set_keep_above(GKeyFile *key, gboolean keep_above);
gboolean ls_keep_above_supported(void);
LsItem *ls_load_id(const char *id, GError **error);
GPtrArray *ls_list(GError **error);
gboolean ls_enable(const char *id, gboolean enabled, GError **error);
gboolean ls_remove(const char *id, GError **error);
char **ls_argv(GKeyFile *key, GError **error);
GSubprocess *ls_spawn(GKeyFile *key, GError **error);
gboolean ls_run(GKeyFile *key, GError **error);
gboolean ls_trial(GKeyFile *key, GError **error);
gboolean ls_watchdog_supported(GKeyFile *key);
gboolean ls_watchdog_set(const char *id, gboolean enabled, GError **error);
gboolean ls_keep_above_set(const char *id, gboolean enabled, GError **error);
gboolean ls_watchdog_sync_autostart(GError **error);
gboolean ls_watchdog_autostart_enabled(void);
gboolean ls_watchdog_is_running(void);
int ls_watchdog_run(void);
void ls_item_free(LsItem *item);
GPtrArray *ls_installed_apps(GError **error);
gboolean ls_create_desktop_shortcut(const LsInstalledApp *app, GError **error);
gboolean ls_desktop_shortcut_exists(const LsInstalledApp *app);
gboolean ls_create_desktop_shortcut_in(const char *source, const char *desktop_id,
                                       const char *desktop_dir, GError **error);
void ls_installed_app_free(LsInstalledApp *app);
int ls_ui(int argc, char **argv);
