#include "core.h"
#include <string.h>

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--watchdog")) return ls_watchdog_run();
    if (argc < 2 || !strcmp(argv[1], "--ui")) return ls_ui(argc > 1 ? 1 : argc, argv);
    g_autoptr(GError) error = NULL;
    if (!strcmp(argv[1], "--version")) { g_print("login-startup 0.2.2\n"); return 0; }
    if (!strcmp(argv[1], "--list-installed-apps")) {
        g_autoptr(GPtrArray) apps = ls_installed_apps(&error);
        if (apps) for (guint i = 0; i < apps->len; ++i) {
            LsInstalledApp *app = g_ptr_array_index(apps, i);
            g_print("%s\t%s\t%s\n", app->desktop_id, app->package, app->name);
        }
    } else if (argc == 3 && (!strcmp(argv[1], "--watchdog-enable") || !strcmp(argv[1], "--watchdog-disable"))) {
        ls_watchdog_set(argv[2], !strcmp(argv[1], "--watchdog-enable"), &error);
    } else if (!strcmp(argv[1], "--list")) {
        g_autoptr(GPtrArray) items = ls_list(&error);
        if (items) for (guint i = 0; i < items->len; ++i) {
            LsItem *item = g_ptr_array_index(items, i);
            g_autofree char *id = g_key_file_get_string(item->key, LS_GROUP, LS_ID, NULL);
            g_autofree char *name = g_key_file_get_string(item->key, LS_GROUP, "Name", NULL);
            g_print("%s\t%s\t%s\n", id, g_key_file_get_boolean(item->key, LS_GROUP, "Hidden", NULL) ? "disabled" : "enabled", name);
        }
    } else if ((!strcmp(argv[1], "--add") || !strcmp(argv[1], "--inspect")) && argc >= 3) {
        const char *name = NULL, *args = NULL, *cwd = NULL; gboolean terminal = FALSE;
        for (int i = 3; i < argc; ++i) {
            if (!strcmp(argv[i], "--terminal")) terminal = TRUE;
            else if (i + 1 < argc && !strcmp(argv[i], "--name")) name = argv[++i];
            else if (i + 1 < argc && !strcmp(argv[i], "--args")) args = argv[++i];
            else if (i + 1 < argc && !strcmp(argv[i], "--cwd")) cwd = argv[++i];
            else { g_printerr("未知参数：%s\n", argv[i]); return 2; }
        }
        g_autoptr(GKeyFile) key = ls_draft(argv[2], name, args, cwd, terminal, &error);
        if (key && (!strcmp(argv[1], "--inspect") || ls_save(key, &error))) {
            g_autofree char *id = g_key_file_get_string(key, LS_GROUP, LS_ID, NULL);
            g_print("%s\n", id);
        }
    } else if (argc == 3 && (!strcmp(argv[1], "--enable") || !strcmp(argv[1], "--disable"))) {
        ls_enable(argv[2], !strcmp(argv[1], "--enable"), &error);
    } else if (argc == 3 && !strcmp(argv[1], "--remove")) {
        ls_remove(argv[2], &error);
    } else if (argc == 3 && (!strcmp(argv[1], "--launch") || !strcmp(argv[1], "--trial-launch") || !strcmp(argv[1], "--try"))) {
        LsItem *item = ls_load_id(argv[2], &error);
        if (item) {
            gboolean hidden = g_key_file_get_boolean(item->key, LS_GROUP, "Hidden", NULL);
            if (!strcmp(argv[1], "--try")) ls_trial(item->key, &error);
            else if (!hidden || !strcmp(argv[1], "--trial-launch")) ls_run(item->key, &error);
            ls_item_free(item);
        }
    } else {
        g_printerr("用法：login-startup [--ui | --watchdog | --list | --list-installed-apps | --add 文件 [--name 名称] [--args 参数] [--cwd 目录] [--terminal] | --inspect 文件 | --enable ID | --disable ID | --watchdog-enable ID | --watchdog-disable ID | --remove ID | --try ID]\n");
        return 2;
    }
    if (error) { g_printerr("%s\n", error->message); return 1; }
    return 0;
}
