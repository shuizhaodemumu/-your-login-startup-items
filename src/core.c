#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean fail(GError **error, const char *message) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, message);
    return FALSE;
}
static char *value(GKeyFile *key, const char *name) {
    return g_key_file_get_string(key, LS_GROUP, name, NULL);
}
char *ls_directory(void) { return g_build_filename(g_get_user_config_dir(), "autostart", NULL); }
char *ls_id_for_source(const char *source) {
    g_autofree char *canonical = g_canonicalize_filename(source, NULL);
    return g_compute_checksum_for_string(G_CHECKSUM_SHA256, canonical, -1);
}
gboolean ls_valid_id(const char *id) {
    if (!id || strlen(id) != 64) return FALSE;
    for (const char *p = id; *p; ++p)
        if (!g_ascii_isdigit(*p) && !(*p >= 'a' && *p <= 'f')) return FALSE;
    return TRUE;
}
static char *path_for(const char *id) {
    g_autofree char *dir = ls_directory();
    g_autofree char *base = g_strdup_printf(LS_PREFIX "%s.desktop", id);
    return g_build_filename(dir, base, NULL);
}
void ls_item_free(LsItem *item) {
    if (!item) return;
    g_free(item->path); g_key_file_unref(item->key); g_free(item);
}
LsItem *ls_load_id(const char *id, GError **error) {
    if (!ls_valid_id(id)) { fail(error, "启动项标识无效"); return NULL; }
    g_autofree char *path = path_for(id);
    struct stat st;
    if (g_lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        fail(error, "启动项不存在，或不是可管理的普通文件"); return NULL;
    }
    g_autoptr(GKeyFile) key = g_key_file_new();
    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, error)) return NULL;
    g_autofree char *stored = value(key, LS_ID);
    g_autofree char *source = value(key, LS_SOURCE);
    g_autofree char *expected = source ? ls_id_for_source(source) : NULL;
    if (!g_key_file_get_boolean(key, LS_GROUP, LS_OWNER, NULL) || g_strcmp0(stored, id) || g_strcmp0(expected, id)) {
        fail(error, "这个启动项不属于登录自启助手"); return NULL;
    }
    LsItem *item = g_new0(LsItem, 1);
    item->path = g_steal_pointer(&path); item->key = g_steal_pointer(&key);
    return item;
}
GPtrArray *ls_list(GError **error) {
    GPtrArray *items = g_ptr_array_new_with_free_func((GDestroyNotify)ls_item_free);
    g_autofree char *dir = ls_directory();
    if (!g_file_test(dir, G_FILE_TEST_EXISTS)) return items;
    g_autoptr(GDir) handle = g_dir_open(dir, 0, error);
    if (!handle) { g_ptr_array_unref(items); return NULL; }
    const char *name;
    while ((name = g_dir_read_name(handle))) {
        if (!g_str_has_prefix(name, LS_PREFIX) || !g_str_has_suffix(name, ".desktop")) continue;
        g_autofree char *id = g_strndup(name + strlen(LS_PREFIX), strlen(name) - strlen(LS_PREFIX) - 8);
        LsItem *item = ls_load_id(id, NULL);
        if (item) g_ptr_array_add(items, item);
    }
    return items;
}
static char *kind_for(const char *path) {
    if (g_str_has_suffix(path, ".desktop")) return g_strdup("desktop");
    char line[4096] = {0};
    FILE *file = g_fopen(path, "r");
    if (file) { if (!fgets(line, sizeof line, file)) line[0] = '\0'; fclose(file); }
    if (!memcmp(line, "\177ELF", 4) || g_str_has_suffix(path, ".AppImage")) return g_strdup("program");
    if (g_str_has_prefix(line, "#!") || g_str_has_suffix(path, ".sh") || g_str_has_suffix(path, ".py")) return g_strdup("script");
    return g_strdup(g_file_test(path, G_FILE_TEST_IS_EXECUTABLE) ? "program" : "document");
}
char **ls_argv(GKeyFile *key, GError **error) {
    g_autofree char *source = value(key, LS_SOURCE);
    g_autofree char *kind = value(key, LS_KIND);
    if (!source || !kind) { fail(error, "缺少文件路径或启动方式"); return NULL; }
    g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func(g_free);
    if (!strcmp(kind, "document")) {
        g_ptr_array_add(argv, g_strdup("/usr/bin/xdg-open"));
    } else if (!strcmp(kind, "script")) {
        char line[4096] = {0};
        FILE *file = g_fopen(source, "r");
        if (!file) { fail(error, "无法读取脚本"); return NULL; }
        if (!fgets(line, sizeof line, file)) line[0] = '\0';
        fclose(file);
        if (g_str_has_prefix(line, "#!")) {
            g_strchomp(line);
            int count = 0; char **interpreter = NULL;
            if (!g_shell_parse_argv(line + 2, &count, &interpreter, error)) return NULL;
            for (int i = 0; i < count; i++) g_ptr_array_add(argv, g_strdup(interpreter[i]));
            g_strfreev(interpreter);
        } else {
            g_ptr_array_add(argv, g_strdup(g_str_has_suffix(source, ".py") ? "python3" : "/bin/sh"));
        }
    } else if (strcmp(kind, "program")) {
        fail(error, "这种文件应由桌面应用启动器处理"); return NULL;
    }
    g_ptr_array_add(argv, g_strdup(source));
    gsize count = 0;
    g_auto(GStrv) arguments = g_key_file_get_string_list(key, LS_GROUP, LS_ARGS, &count, NULL);
    if (!strcmp(kind, "document") && count) { fail(error, "普通文档不支持启动参数"); return NULL; }
    for (gsize i = 0; i < count; i++) g_ptr_array_add(argv, g_strdup(arguments[i]));
    g_autofree char *program = g_find_program_in_path(g_ptr_array_index(argv, 0));
    if (!program) { fail(error, "程序或脚本解释器不存在，或没有执行权限"); return NULL; }
    g_ptr_array_add(argv, NULL);
    return (char **)g_ptr_array_free(g_steal_pointer(&argv), FALSE);
}
gboolean ls_validate(GKeyFile *key, GError **error) {
    g_autofree char *source = value(key, LS_SOURCE);
    g_autofree char *cwd = value(key, "Path");
    g_autofree char *kind = value(key, LS_KIND);
    if (!source || !g_path_is_absolute(source) || !g_file_test(source, G_FILE_TEST_IS_REGULAR)) return fail(error, "原文件已移动、删除，或不是普通文件");
    if (cwd && *cwd && !g_file_test(cwd, G_FILE_TEST_IS_DIR)) return fail(error, "工作目录不存在");
    if (!g_strcmp0(kind, "desktop")) {
        g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new_from_keyfile(key);
        if (!info) return fail(error, "应用快捷方式无效，或 Exec / TryExec 指向的程序不存在");
        return TRUE;
    }
    if (!g_strcmp0(kind, "program") && !g_file_test(source, G_FILE_TEST_IS_EXECUTABLE)) return fail(error, "程序没有执行权限；请先在文件属性中允许执行");
    g_auto(GStrv) argv = ls_argv(key, error);
    return argv != NULL;
}
GKeyFile *ls_draft(const char *source, const char *name, const char *args, const char *cwd, gboolean terminal, GError **error) {
    if (!source || !*source) { fail(error, "请先选择文件"); return NULL; }
    g_autofree char *absolute = g_canonicalize_filename(source, NULL);
    g_autofree char *kind = kind_for(absolute);
    g_autofree char *id = ls_id_for_source(absolute);
    g_autoptr(GKeyFile) key = g_key_file_new();
    if (!strcmp(kind, "desktop")) {
        if (!g_key_file_load_from_file(key, absolute, G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, error)) return NULL;
        g_autofree char *type = value(key, "Type");
        if (g_strcmp0(type, "Application")) { fail(error, "只支持 Type=Application 的桌面快捷方式"); return NULL; }
        if (args && *args) { fail(error, "应用快捷方式保留原始参数，请勿在这里追加参数"); return NULL; }
    } else {
        g_key_file_set_string(key, LS_GROUP, "Type", "Application");
        g_autofree char *exec = g_strdup_printf("/usr/bin/login-startup --launch %s", id);
        g_key_file_set_string(key, LS_GROUP, "Exec", exec);
        g_key_file_set_boolean(key, LS_GROUP, "Terminal", terminal);
        g_key_file_set_string(key, LS_GROUP, "Icon", !strcmp(kind, "document") ? "text-x-generic" : "application-x-executable");
        if (args && *args) {
            int count; char **parsed;
            if (!g_shell_parse_argv(args, &count, &parsed, error)) return NULL;
            g_key_file_set_string_list(key, LS_GROUP, LS_ARGS, (const char * const *)parsed, count);
            g_strfreev(parsed);
        }
    }
    if (name && *name) g_key_file_set_string(key, LS_GROUP, "Name", name);
    else if (!g_key_file_has_key(key, LS_GROUP, "Name", NULL)) {
        g_autofree char *base = g_path_get_basename(absolute);
        g_key_file_set_string(key, LS_GROUP, "Name", base);
    }
    if (cwd && *cwd) {
        g_autofree char *canonical = g_canonicalize_filename(cwd, NULL);
        g_key_file_set_string(key, LS_GROUP, "Path", canonical);
    } else if (strcmp(kind, "desktop")) {
        g_autofree char *parent = g_path_get_dirname(absolute);
        g_key_file_set_string(key, LS_GROUP, "Path", parent);
    }
    g_key_file_set_boolean(key, LS_GROUP, "Hidden", FALSE);
    g_key_file_set_boolean(key, LS_GROUP, "X-GNOME-Autostart-enabled", TRUE);
    g_key_file_set_boolean(key, LS_GROUP, LS_OWNER, TRUE);
    g_key_file_set_string(key, LS_GROUP, LS_SOURCE, absolute);
    g_key_file_set_string(key, LS_GROUP, LS_KIND, kind);
    g_key_file_set_string(key, LS_GROUP, LS_ID, id);
    if (!ls_validate(key, error)) return NULL;
    return g_steal_pointer(&key);
}
gboolean ls_save(GKeyFile *key, GError **error) {
    g_autofree char *id = value(key, LS_ID);
    g_autofree char *source = value(key, LS_SOURCE);
    g_autofree char *expected = source ? ls_id_for_source(source) : NULL;
    if (!ls_valid_id(id) || g_strcmp0(id, expected) || !g_key_file_get_boolean(key, LS_GROUP, LS_OWNER, NULL)) return fail(error, "拒绝保存不属于本软件的启动项");
    g_autofree char *dir = ls_directory();
    if (g_mkdir_with_parents(dir, 0700)) { g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "无法创建自启动目录：%s", g_strerror(errno)); return FALSE; }
    g_autofree char *path = path_for(id);
    struct stat st;
    if (g_lstat(path, &st) == 0) {
        LsItem *old = ls_load_id(id, error);
        if (!old) return FALSE;
        ls_item_free(old);
    }
    gsize size;
    g_autofree char *data = g_key_file_to_data(key, &size, error);
    return data && g_file_set_contents_full(path, data, size, G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
}
gboolean ls_enable(const char *id, gboolean enabled, GError **error) {
    LsItem *item = ls_load_id(id, error);
    if (!item) return FALSE;
    gboolean result = !enabled || ls_validate(item->key, error);
    if (result) {
        g_key_file_set_boolean(item->key, LS_GROUP, "Hidden", !enabled);
        g_key_file_set_boolean(item->key, LS_GROUP, "X-GNOME-Autostart-enabled", enabled);
        result = ls_save(item->key, error);
    }
    ls_item_free(item); return result;
}
gboolean ls_remove(const char *id, GError **error) {
    LsItem *item = ls_load_id(id, error);
    if (!item) return FALSE;
    int result = g_unlink(item->path);
    if (result) g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "无法删除启动配置：%s", g_strerror(errno));
    ls_item_free(item); return result == 0;
}
GSubprocess *ls_spawn(GKeyFile *key, GError **error) {
    if (!ls_validate(key, error)) return NULL;
    g_auto(GStrv) argv = ls_argv(key, error);
    if (!argv) return NULL;
    g_autofree char *cwd = value(key, "Path");
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    if (cwd && *cwd) g_subprocess_launcher_set_cwd(launcher, cwd);
    return g_subprocess_launcher_spawnv(launcher, (const char * const *)argv, error);
}
gboolean ls_run(GKeyFile *key, GError **error) {
    g_autoptr(GSubprocess) process = ls_spawn(key, error);
    return process && g_subprocess_wait_check(process, NULL, error);
}
gboolean ls_trial(GKeyFile *key, GError **error) {
    if (!ls_validate(key, error)) return FALSE;
    g_autoptr(GKeyFile) copy = g_key_file_new();
    gsize size;
    g_autofree char *data = g_key_file_to_data(key, &size, error);
    if (!data || !g_key_file_load_from_data(copy, data, size, G_KEY_FILE_NONE, error)) return FALSE;
    g_key_file_set_boolean(copy, LS_GROUP, "Hidden", FALSE);
    g_key_file_set_boolean(copy, LS_GROUP, "X-GNOME-Autostart-enabled", TRUE);
    g_autofree char *kind = value(copy, LS_KIND);
    if (g_strcmp0(kind, "desktop")) {
        g_autofree char *id = value(copy, LS_ID);
        if (!ls_valid_id(id)) return fail(error, "启动项标识无效");
        g_autofree char *exec = g_strdup_printf("/usr/bin/login-startup --trial-launch %s", id);
        g_key_file_set_string(copy, LS_GROUP, "Exec", exec);
    }
    g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new_from_keyfile(copy);
    if (!info) return fail(error, "无法解析启动命令");
    return g_app_info_launch(G_APP_INFO(info), NULL, NULL, error);
}
