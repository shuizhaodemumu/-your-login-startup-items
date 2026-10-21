#pragma once
#include <gio/gdesktopappinfo.h>

#define LS_GROUP "Desktop Entry"
#define LS_PREFIX "login-startup-"
#define LS_OWNER "X-LoginStartup-Managed"
#define LS_SOURCE "X-LoginStartup-Source"
#define LS_KIND "X-LoginStartup-Kind"
#define LS_ARGS "X-LoginStartup-Arguments"
#define LS_ID "X-LoginStartup-Id"

typedef struct { char *path; GKeyFile *key; } LsItem;
char *ls_directory(void);
char *ls_id_for_source(const char *source);
gboolean ls_valid_id(const char *id);
GKeyFile *ls_draft(const char *source, const char *name, const char *args,
                  const char *cwd, gboolean terminal, GError **error);
gboolean ls_validate(GKeyFile *key, GError **error);
gboolean ls_save(GKeyFile *key, GError **error);
LsItem *ls_load_id(const char *id, GError **error);
GPtrArray *ls_list(GError **error);
gboolean ls_enable(const char *id, gboolean enabled, GError **error);
gboolean ls_remove(const char *id, GError **error);
char **ls_argv(GKeyFile *key, GError **error);
GSubprocess *ls_spawn(GKeyFile *key, GError **error);
gboolean ls_run(GKeyFile *key, GError **error);
gboolean ls_trial(GKeyFile *key, GError **error);
void ls_item_free(LsItem *item);
int ls_ui(int argc, char **argv);
