#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "core.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static gboolean fail(GError **error, const char *message) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, message);
    return FALSE;
}
static gboolean start_watchdog_daemon(GError **error);
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
    /* Self-contained .run launchers often use a shell header but exec a long-lived app. */
    if (g_str_has_suffix(path, ".run") && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE)) return g_strdup("program");
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
        if (result && !ls_watchdog_sync_autostart(error)) result = FALSE;
        if (result && enabled && g_key_file_get_boolean(item->key, LS_GROUP, LS_WATCHDOG, NULL))
            result = start_watchdog_daemon(error);
    }
    ls_item_free(item); return result;
}
gboolean ls_remove(const char *id, GError **error) {
    LsItem *item = ls_load_id(id, error);
    if (!item) return FALSE;
    int result = g_unlink(item->path);
    if (result) g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "无法删除启动配置：%s", g_strerror(errno));
    if (!result && !ls_watchdog_sync_autostart(error)) result = -1;
    ls_item_free(item); return result == 0;
}
GSubprocess *ls_spawn(GKeyFile *key, GError **error) {
    if (!ls_validate(key, error)) return NULL;
    g_auto(GStrv) argv = ls_argv(key, error);
    if (!argv) return NULL;
    g_autofree char *cwd = value(key, "Path");
    g_autoptr(GSubprocessLauncher) launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    if (cwd && *cwd) g_subprocess_launcher_set_cwd(launcher, cwd);
    g_autofree char *kind = value(key, LS_KIND);
    g_autofree char *source = value(key, LS_SOURCE);
    if (!g_strcmp0(kind, "program") && source && g_str_has_suffix(source, ".run")) {
        g_autofree char *canonical = g_canonicalize_filename(source, NULL);
        g_subprocess_launcher_setenv(launcher, "LOGIN_STARTUP_WATCHDOG_SOURCE", canonical, TRUE);
    }
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

static char *resolved_watchdog_executable(GKeyFile *key) {
    g_autofree char *kind = value(key, LS_KIND);
    g_autofree char *source = value(key, LS_SOURCE);
    if (!source) return NULL;
    if (!g_strcmp0(kind, "program")) return realpath(source, NULL);
    if (g_strcmp0(kind, "desktop")) return NULL;
    g_autofree char *exec = g_key_file_get_string(key, LS_GROUP, "Exec", NULL);
    if (!exec) return NULL;
    int argc = 0; char **argv = NULL;
    if (!g_shell_parse_argv(exec, &argc, &argv, NULL) || argc < 1) { g_strfreev(argv); return NULL; }
    const char *command = argv[0];
    const char *wrappers[] = { "env", "sh", "bash", "dash", "zsh", "flatpak", "snap", "gtk-launch", "gio", NULL };
    for (guint i = 0; wrappers[i]; ++i) {
        if (!g_strcmp0(command, wrappers[i]) ||
            g_str_has_suffix(command, G_DIR_SEPARATOR_S "env") ||
            g_str_has_suffix(command, G_DIR_SEPARATOR_S "sh") ||
            g_str_has_suffix(command, G_DIR_SEPARATOR_S "bash")) {
            g_strfreev(argv); return NULL;
        }
    }
    g_autofree char *resolved = g_find_program_in_path(command);
    g_strfreev(argv);
    return resolved ? realpath(resolved, NULL) : NULL;
}

gboolean ls_watchdog_supported(GKeyFile *key) {
    g_autofree char *kind = value(key, LS_KIND);
    g_autofree char *source = value(key, LS_SOURCE);
    if (!source || !g_file_test(source, G_FILE_TEST_IS_REGULAR)) return FALSE;
    if (g_strcmp0(kind, "program") && g_strcmp0(kind, "desktop")) return FALSE;
    g_autofree char *executable = resolved_watchdog_executable(key);
    return executable && g_file_test(executable, G_FILE_TEST_IS_EXECUTABLE);
}

typedef struct {
    off_t size;
    time_t mtime;
    time_t ctime;
    char *digest;
} RunFileIdentity;

static GHashTable *run_file_identities;

static void run_file_identity_free(RunFileIdentity *identity) {
    if (!identity) return;
    g_free(identity->digest);
    g_free(identity);
}

static void run_file_payload_range(const char *path, off_t file_size, off_t *start, off_t *length) {
    *start = 0;
    *length = file_size;
    FILE *file = g_fopen(path, "rb");
    if (!file) return;
    char header[65537];
    size_t size = fread(header, 1, sizeof header - 1, file);
    fclose(file);
    header[size] = '\0';
    /* Self-extracting launchers have binary payload bytes in this header buffer. */
    g_autoptr(GRegex) regex = g_regex_new("tail[ \\t]+-c[ \\t]+\\+([0-9]+)[^\\n]*head[ \\t]+-c[ \\t]+([0-9]+)", G_REGEX_RAW, 0, NULL);
    if (!regex) return;
    g_autoptr(GMatchInfo) match = NULL;
    if (!g_regex_match(regex, header, 0, &match)) return;
    g_autofree char *offset_text = g_match_info_fetch(match, 1);
    g_autofree char *length_text = g_match_info_fetch(match, 2);
    char *end = NULL;
    guint64 offset = g_ascii_strtoull(offset_text, &end, 10);
    if (!end || *end || offset < 1) return;
    guint64 payload_size = g_ascii_strtoull(length_text, &end, 10);
    if (!end || *end || !payload_size || offset - 1 > G_MAXUINT64 - payload_size) return;
    guint64 payload_start = offset - 1;
    if (payload_start + payload_size > (guint64)file_size) return;
    *start = (off_t)payload_start;
    *length = (off_t)payload_size;
}

static char *run_file_digest(const char *path) {
    struct stat st;
    if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return NULL;
    off_t payload_start = 0, payload_length = st.st_size;
    run_file_payload_range(path, st.st_size, &payload_start, &payload_length);
    if (!run_file_identities)
        run_file_identities = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                                     (GDestroyNotify)run_file_identity_free);
    RunFileIdentity *cached = g_hash_table_lookup(run_file_identities, path);
    if (cached && cached->size == st.st_size && cached->mtime == st.st_mtime && cached->ctime == st.st_ctime)
        return g_strdup(cached->digest);
    FILE *file = g_fopen(path, "rb");
    if (!file) return NULL;
    if (fseeko(file, payload_start, SEEK_SET) != 0) { fclose(file); return NULL; }
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    guchar buffer[64 * 1024];
    size_t count;
    off_t remaining = payload_length;
    while (remaining > 0 && (count = fread(buffer, 1, MIN(sizeof buffer, (gsize)remaining), file)) > 0) {
        g_checksum_update(checksum, buffer, count);
        remaining -= count;
    }
    gboolean failed = ferror(file) != 0;
    fclose(file);
    if (failed || remaining > 0) { g_checksum_free(checksum); return NULL; }
    RunFileIdentity *identity = g_new0(RunFileIdentity, 1);
    identity->size = st.st_size;
    identity->mtime = st.st_mtime;
    identity->ctime = st.st_ctime;
    identity->digest = g_strdup(g_checksum_get_string(checksum));
    g_checksum_free(checksum);
    g_hash_table_replace(run_file_identities, g_strdup(path), identity);
    return g_strdup(identity->digest);
}

static gboolean run_files_match(const char *source, const char *candidate) {
    if (!g_strcmp0(source, candidate)) return TRUE;
    g_autofree char *source_digest = run_file_digest(source);
    if (!source_digest) return FALSE;
    g_autofree char *candidate_digest = run_file_digest(candidate);
    return candidate_digest && !g_strcmp0(source_digest, candidate_digest);
}

static gboolean process_has_env_path(const char *process_dir, const char *source, const char *canonical) {
    g_autofree char *env_path = g_build_filename(process_dir, "environ", NULL);
    g_autofree char *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(env_path, &contents, &length, NULL)) return FALSE;
    for (gsize offset = 0; offset < length;) {
        const char *entry = contents + offset;
        gsize entry_len = strnlen(entry, length - offset);
        const char *equals = memchr(entry, '=', entry_len);
        if (equals) {
            const char *env_value = equals + 1;
            if (!strcmp(env_value, canonical)) return TRUE;
            if (g_path_is_absolute(env_value) && g_str_has_suffix(env_value, ".run")) {
                gboolean matches = run_files_match(source, env_value);
                if (matches) return TRUE;
            }
        }
        offset += entry_len + 1;
    }
    return FALSE;
}

static char *watchdog_autostart_path(void) {
    g_autofree char *dir = ls_directory();
    return g_build_filename(dir, LS_WATCHDOG_AUTOSTART, NULL);
}

gboolean ls_watchdog_sync_autostart(GError **error) {
    g_autoptr(GPtrArray) items = ls_list(error);
    if (!items) return FALSE;
    gboolean needed = FALSE;
    for (guint i = 0; i < items->len; ++i) {
        LsItem *item = g_ptr_array_index(items, i);
        if (!g_key_file_get_boolean(item->key, LS_GROUP, "Hidden", NULL) &&
            g_key_file_get_boolean(item->key, LS_GROUP, LS_WATCHDOG, NULL) &&
            ls_watchdog_supported(item->key)) { needed = TRUE; break; }
    }
    g_autofree char *path = watchdog_autostart_path();
    struct stat st;
    gboolean exists = g_lstat(path, &st) == 0;
    if (!needed) {
        if (!exists) return TRUE;
        if (!S_ISREG(st.st_mode)) return fail(error, "看门狗自启动项不是普通文件，拒绝删除");
        g_autoptr(GKeyFile) old = g_key_file_new();
        if (!g_key_file_load_from_file(old, path, G_KEY_FILE_NONE, error)) return FALSE;
        if (!g_key_file_get_boolean(old, LS_GROUP, "X-LoginStartup-Watchdog-Owner", NULL))
            return fail(error, "看门狗自启动项不属于登录自启助手，拒绝删除");
        if (g_unlink(path) != 0) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "无法移除看门狗自启动项：%s", g_strerror(errno));
            return FALSE;
        }
        return TRUE;
    }
    if (exists) {
        if (!S_ISREG(st.st_mode)) return fail(error, "看门狗自启动路径不是普通文件，拒绝覆盖");
        g_autoptr(GKeyFile) old = g_key_file_new();
        if (!g_key_file_load_from_file(old, path, G_KEY_FILE_NONE, error)) return FALSE;
        if (!g_key_file_get_boolean(old, LS_GROUP, "X-LoginStartup-Watchdog-Owner", NULL))
            return fail(error, "看门狗自启动项不属于登录自启助手，拒绝覆盖");
    }
    const char *binary = g_find_program_in_path("login-startup");
    if (!binary) binary = "/usr/bin/login-startup";
    if (!g_file_test(binary, G_FILE_TEST_IS_EXECUTABLE))
        return fail(error, "找不到已安装的登录自启助手，无法启用登录时看门狗");
    g_autofree char *dir = ls_directory();
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "无法创建自启动目录：%s", g_strerror(errno));
        return FALSE;
    }
    g_autoptr(GKeyFile) desktop = g_key_file_new();
    g_key_file_set_string(desktop, LS_GROUP, "Type", "Application");
    g_key_file_set_string(desktop, LS_GROUP, "Name", "登录自启助手看门狗");
    g_key_file_set_string(desktop, LS_GROUP, "Comment", "监控已启用看门狗的登录启动项");
    g_autofree char *exec = g_strdup_printf("%s --watchdog", binary);
    g_key_file_set_string(desktop, LS_GROUP, "Exec", exec);
    g_key_file_set_string(desktop, LS_GROUP, "Icon", "io.github.local.LoginStartup");
    g_key_file_set_boolean(desktop, LS_GROUP, "Terminal", FALSE);
    g_key_file_set_boolean(desktop, LS_GROUP, "Hidden", FALSE);
    g_key_file_set_boolean(desktop, LS_GROUP, "X-GNOME-Autostart-enabled", TRUE);
    g_key_file_set_boolean(desktop, LS_GROUP, "X-LoginStartup-Watchdog-Owner", TRUE);
    gsize length = 0;
    g_autofree char *contents = g_key_file_to_data(desktop, &length, error);
    if (!contents) return FALSE;
    return g_file_set_contents_full(path, contents, length,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
}

gboolean ls_watchdog_autostart_enabled(void) {
    g_autofree char *path = watchdog_autostart_path();
    g_autoptr(GKeyFile) desktop = g_key_file_new();
    return g_key_file_load_from_file(desktop, path, G_KEY_FILE_NONE, NULL) &&
           g_key_file_get_boolean(desktop, LS_GROUP, "X-LoginStartup-Watchdog-Owner", NULL) &&
           !g_key_file_get_boolean(desktop, LS_GROUP, "Hidden", NULL) &&
           g_key_file_get_boolean(desktop, LS_GROUP, "X-GNOME-Autostart-enabled", NULL);
}

gboolean ls_watchdog_is_running(void) {
    g_autofree char *cache = g_build_filename(g_get_user_cache_dir(), "login-startup", NULL);
    g_autofree char *lock_path = g_build_filename(cache, "watchdog.lock", NULL);
    int fd = g_open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) return FALSE;
    gboolean running = flock(fd, LOCK_EX | LOCK_NB) != 0;
    if (!running) flock(fd, LOCK_UN);
    close(fd);
    return running;
}

static gboolean start_watchdog_daemon(GError **error) {
    const char *binary = g_find_program_in_path("login-startup");
    if (!binary) binary = "/usr/bin/login-startup";
    const char *argv[] = { binary, "--watchdog", NULL };
    GError *spawn_error = NULL;
    if (!g_spawn_async(NULL, (char **)argv, NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
        NULL, NULL, NULL, &spawn_error)) {
        g_propagate_prefixed_error(error, spawn_error, "无法启动看门狗后台进程：");
        return FALSE;
    }
    return TRUE;
}

gboolean ls_watchdog_set(const char *id, gboolean enabled, GError **error) {
    LsItem *item = ls_load_id(id, error);
    if (!item) return FALSE;
    if (enabled && !ls_watchdog_supported(item->key)) {
        ls_item_free(item);
        return fail(error, "看门狗支持直接启动的程序、可识别进程的应用快捷方式，以及有执行权限的 .run 启动器");
    }
    if (enabled && g_key_file_get_boolean(item->key, LS_GROUP, "Hidden", NULL)) {
        ls_item_free(item);
        return fail(error, "请先启用这个启动项，再开启看门狗");
    }
    if (enabled) {
        g_autofree char *target = resolved_watchdog_executable(item->key);
        g_autoptr(GPtrArray) items = ls_list(error);
        if (!items) { ls_item_free(item); return FALSE; }
        for (guint i = 0; i < items->len; ++i) {
            LsItem *other = g_ptr_array_index(items, i);
            g_autofree char *other_id = value(other->key, LS_ID);
            if (!g_strcmp0(other_id, id) ||
                g_key_file_get_boolean(other->key, LS_GROUP, "Hidden", NULL) ||
                !g_key_file_get_boolean(other->key, LS_GROUP, LS_WATCHDOG, NULL)) continue;
            g_autofree char *other_target = resolved_watchdog_executable(other->key);
            if (target && other_target && !g_strcmp0(target, other_target)) {
                ls_item_free(item);
                return fail(error, "这个程序已经由另一个看门狗监控，避免重复启动，请先关闭另一项");
            }
        }
    }
    gboolean previous = g_key_file_get_boolean(item->key, LS_GROUP, LS_WATCHDOG, NULL);
    g_key_file_set_boolean(item->key, LS_GROUP, LS_WATCHDOG, enabled);
    gboolean saved = ls_save(item->key, error);
    ls_item_free(item);
    if (!saved) return FALSE;
    if (!ls_watchdog_sync_autostart(error)) {
        LsItem *restore = ls_load_id(id, NULL);
        if (restore) {
            g_key_file_set_boolean(restore->key, LS_GROUP, LS_WATCHDOG, previous);
            ls_save(restore->key, NULL);
            ls_item_free(restore);
        }
        return FALSE;
    }
    if (enabled && !start_watchdog_daemon(error)) return FALSE;
    return TRUE;
}

typedef struct {
    gboolean running;
    gboolean launching;
    gint64 launch_deadline;
    gint64 retry_at;
    guint backoff_seconds;
} WatchdogState;

typedef struct { GMainLoop *loop; GHashTable *states; int lock_fd; } Watchdog;

static gboolean process_matches(GKeyFile *key) {
    g_autofree char *source = value(key, LS_SOURCE);
    g_autofree char *kind = value(key, LS_KIND);
    g_autofree char *executable = resolved_watchdog_executable(key);
    g_autofree char *canonical = g_canonicalize_filename(source, NULL);
    g_autoptr(GDir) proc = g_dir_open("/proc", 0, NULL);
    if (!proc) return FALSE;
    const char *entry;
    uid_t uid = getuid();
    while ((entry = g_dir_read_name(proc))) {
        if (!g_ascii_isdigit(entry[0])) continue;
        char *end = NULL;
        long pid = strtol(entry, &end, 10);
        if (!end || *end || pid <= 1 || pid == getpid()) continue;
        g_autofree char *process_dir = g_build_filename("/proc", entry, NULL);
        struct stat st;
        if (g_stat(process_dir, &st) != 0 || st.st_uid != uid) continue;
        if (!g_strcmp0(kind, "program") || !g_strcmp0(kind, "desktop")) {
            if (!executable) return FALSE;
            g_autofree char *exe_link = g_build_filename(process_dir, "exe", NULL);
            char target[PATH_MAX + 1];
            ssize_t length = readlink(exe_link, target, PATH_MAX);
            if (length > 0) {
                target[length] = '\0';
                if (!strcmp(target, executable)) return TRUE;
            }
            if (g_strcmp0(kind, "program")) continue;
            /* AppImages may run from a temporary mount while retaining the source path in argv. */
            g_autofree char *cmdline = g_build_filename(process_dir, "cmdline", NULL);
            g_autofree char *contents = NULL;
            gsize cmdline_length = 0;
            if (!g_file_get_contents(cmdline, &contents, &cmdline_length, NULL)) continue;
            for (gsize i = 0; i < cmdline_length;) {
                const char *arg = contents + i;
                gsize arg_len = strnlen(arg, cmdline_length - i);
                if (arg_len == strlen(canonical) && !memcmp(arg, canonical, arg_len)) return TRUE;
                i += arg_len + 1;
            }
            if (!g_strcmp0(kind, "program") && g_str_has_suffix(source, ".run") &&
                process_has_env_path(process_dir, source, canonical)) return TRUE;
        } else {
            g_autofree char *cmdline = g_build_filename(process_dir, "cmdline", NULL);
            g_autofree char *contents = NULL;
            gsize length = 0;
            if (!g_file_get_contents(cmdline, &contents, &length, NULL)) continue;
            for (gsize i = 0; i < length;) {
                const char *arg = contents + i;
                gsize arg_len = strnlen(arg, length - i);
                if (arg_len == strlen(canonical) && !memcmp(arg, canonical, arg_len)) return TRUE;
                i += arg_len + 1;
            }
        }
    }
    return FALSE;
}

static guint next_backoff(guint current) { return MIN(current ? current * 2 : 2, 60); }

static gboolean watchdog_tick(gpointer data) {
    Watchdog *watchdog = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) items = ls_list(&error);
    if (!items) {
        g_warning("看门狗无法读取启动项：%s", error->message);
        return G_SOURCE_CONTINUE;
    }
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    gboolean any = FALSE;
    gint64 now = g_get_monotonic_time();
    for (guint i = 0; i < items->len; ++i) {
        LsItem *item = g_ptr_array_index(items, i);
        if (g_key_file_get_boolean(item->key, LS_GROUP, "Hidden", NULL) ||
            !g_key_file_get_boolean(item->key, LS_GROUP, LS_WATCHDOG, NULL) ||
            !ls_watchdog_supported(item->key)) continue;
        any = TRUE;
        g_autofree char *id = value(item->key, LS_ID);
        g_autofree char *source = value(item->key, LS_SOURCE);
        g_autofree char *kind = value(item->key, LS_KIND);
        g_hash_table_add(seen, g_strdup(id));
        WatchdogState *state = g_hash_table_lookup(watchdog->states, id);
        if (!state) {
            state = g_new0(WatchdogState, 1);
            state->backoff_seconds = 2;
            g_hash_table_insert(watchdog->states, g_strdup(id), state);
        }
        gboolean running = process_matches(item->key);
        if (running) {
            if (!state->running) state->backoff_seconds = 2;
            state->running = TRUE;
            state->launching = FALSE;
            continue;
        }
        if (state->running) {
            state->running = FALSE;
            state->retry_at = now + (gint64)state->backoff_seconds * G_USEC_PER_SEC;
            state->backoff_seconds = next_backoff(state->backoff_seconds);
            continue;
        }
        if (state->launching) {
            if (now < state->launch_deadline) continue;
            state->launching = FALSE;
            state->retry_at = now + (gint64)state->backoff_seconds * G_USEC_PER_SEC;
            state->backoff_seconds = next_backoff(state->backoff_seconds);
            continue;
        }
        if (now < state->retry_at) continue;
        g_autoptr(GError) launch_error = NULL;
        gboolean launched = FALSE;
        if (!g_strcmp0(kind, "desktop")) {
            launched = ls_trial(item->key, &launch_error);
        } else {
            GSubprocess *process = ls_spawn(item->key, &launch_error);
            if (process) { launched = TRUE; g_object_unref(process); }
        }
        if (!launched) {
            g_warning("看门狗无法启动 %s：%s", source, launch_error->message);
            state->retry_at = now + (gint64)state->backoff_seconds * G_USEC_PER_SEC;
            state->backoff_seconds = next_backoff(state->backoff_seconds);
        } else {
            state->launching = TRUE;
            state->launch_deadline = now + 10 * G_USEC_PER_SEC;
            g_message("看门狗已重新启动：%s", source);
        }
    }
    GHashTableIter iter; gpointer key;
    g_hash_table_iter_init(&iter, watchdog->states);
    while (g_hash_table_iter_next(&iter, &key, NULL))
        if (!g_hash_table_contains(seen, key)) g_hash_table_iter_remove(&iter);
    g_hash_table_unref(seen);
    if (!any) {
        g_autoptr(GError) sync_error = NULL;
        if (!ls_watchdog_sync_autostart(&sync_error)) g_warning("无法清理看门狗自启动项：%s", sync_error->message);
        g_main_loop_quit(watchdog->loop);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

int ls_watchdog_run(void) {
    g_autofree char *cache = g_build_filename(g_get_user_cache_dir(), "login-startup", NULL);
    if (g_mkdir_with_parents(cache, 0700) != 0) return 1;
    g_autofree char *lock_path = g_build_filename(cache, "watchdog.lock", NULL);
    int lock_fd = g_open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd < 0) return 1;
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) { close(lock_fd); return 0; }
    Watchdog watchdog = {0};
    watchdog.lock_fd = lock_fd;
    watchdog.loop = g_main_loop_new(NULL, FALSE);
    watchdog.states = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_timeout_add_seconds(2, watchdog_tick, &watchdog);
    g_main_loop_run(watchdog.loop);
    g_hash_table_unref(watchdog.states);
    g_main_loop_unref(watchdog.loop);
    close(watchdog.lock_fd);
    if (run_file_identities) {
        g_hash_table_unref(run_file_identities);
        run_file_identities = NULL;
    }
    return 0;
}

void ls_installed_app_free(LsInstalledApp *app) {
    if (!app) return;
    g_free(app->path);
    g_free(app->desktop_id);
    g_free(app->name);
    g_free(app->package);
    g_clear_object(&app->icon);
    g_free(app);
}

static char *desktop_package_owner(const char *path, GError **error) {
    const char *owner_argv[] = { "dpkg-query", "-S", "--", path, NULL };
    g_autoptr(GSubprocess) owner_proc = g_subprocess_newv(owner_argv,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, error);
    if (!owner_proc) return NULL;
    g_autofree char *owner_output = NULL;
    if (!g_subprocess_communicate_utf8(owner_proc, NULL, NULL, &owner_output, NULL, error)) return NULL;
    if (!g_subprocess_get_successful(owner_proc) || !owner_output) return NULL;
    g_strstrip(owner_output);
    char *separator = strstr(owner_output, ": ");
    if (!separator || separator == owner_output) return NULL;
    g_autofree char *package = g_strndup(owner_output, separator - owner_output);

    const char *status_argv[] = { "dpkg-query", "-W", "-f=${db:Status-Abbrev}", "--", package, NULL };
    g_autoptr(GSubprocess) status_proc = g_subprocess_newv(status_argv,
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, error);
    if (!status_proc) return NULL;
    g_autofree char *status = NULL;
    if (!g_subprocess_communicate_utf8(status_proc, NULL, NULL, &status, NULL, error)) return NULL;
    if (!g_subprocess_get_successful(status_proc) || !status || !g_str_has_prefix(status, "ii ")) return NULL;
    return g_steal_pointer(&package);
}

static gint installed_app_compare(gconstpointer a, gconstpointer b) {
    const LsInstalledApp *left = *(LsInstalledApp * const *)a;
    const LsInstalledApp *right = *(LsInstalledApp * const *)b;
    return g_utf8_collate(left->name ? left->name : left->desktop_id,
                          right->name ? right->name : right->desktop_id);
}

GPtrArray *ls_installed_apps(GError **error) {
    GPtrArray *apps = g_ptr_array_new_with_free_func((GDestroyNotify)ls_installed_app_free);
    if (!g_find_program_in_path("dpkg-query")) {
        g_ptr_array_unref(apps);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "找不到 dpkg-query，无法确认应用是否由已安装的 deb 包提供");
        return NULL;
    }
    const char *directories[] = { "/usr/share/applications", NULL };
    for (guint d = 0; directories[d]; ++d) {
        g_autoptr(GError) dir_error = NULL;
        GDir *dir = g_dir_open(directories[d], 0, &dir_error);
        if (!dir) {
            if (g_error_matches(dir_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) continue;
            g_propagate_error(error, g_steal_pointer(&dir_error));
            g_ptr_array_unref(apps);
            return NULL;
        }
        const char *name;
        while ((name = g_dir_read_name(dir))) {
            if (!g_str_has_suffix(name, ".desktop")) continue;
            g_autofree char *path = g_build_filename(directories[d], name, NULL);
            struct stat st;
            if (g_lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;

            g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new_from_filename(path);
            if (!info || !g_app_info_should_show(G_APP_INFO(info))) continue;
            g_autofree char *package = desktop_package_owner(path, error);
            if (error && *error) {
                g_dir_close(dir);
                g_ptr_array_unref(apps);
                return NULL;
            }
            if (!package) continue;

            LsInstalledApp *app = g_new0(LsInstalledApp, 1);
            app->path = g_strdup(path);
            app->desktop_id = g_strdup(name);
            app->name = g_strdup(g_app_info_get_name(G_APP_INFO(info)));
            app->package = g_steal_pointer(&package);
            GIcon *icon = g_app_info_get_icon(G_APP_INFO(info));
            app->icon = icon ? g_object_ref(icon) : NULL;
            app->shortcut_exists = ls_desktop_shortcut_exists(app);
            g_ptr_array_add(apps, app);
        }
        g_dir_close(dir);
    }
    g_ptr_array_sort(apps, installed_app_compare);
    return apps;
}

static char *desktop_shortcut_path(const char *desktop_id, const char *desktop_dir) {
    g_autofree char *base = g_path_get_basename(desktop_id);
    g_autofree char *name = g_strdup_printf("login-startup-shortcut-%s", base);
    return g_build_filename(desktop_dir, name, NULL);
}

gboolean ls_desktop_shortcut_exists(const LsInstalledApp *app) {
    if (!app) return FALSE;
    const char *desktop_dir = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    if (!desktop_dir || !g_path_is_absolute(desktop_dir)) return FALSE;
    g_autofree char *path = desktop_shortcut_path(app->desktop_id, desktop_dir);
    return g_file_test(path, G_FILE_TEST_EXISTS);
}

gboolean ls_create_desktop_shortcut_in(const char *source, const char *desktop_id,
                                       const char *desktop_dir, GError **error) {
    if (!source || !g_path_is_absolute(source) || !desktop_id || !*desktop_id ||
        !desktop_dir || !g_path_is_absolute(desktop_dir)) {
        return fail(error, "应用启动器或桌面目录路径无效");
    }
    g_autoptr(GDesktopAppInfo) info = g_desktop_app_info_new_from_filename(source);
    if (!info || !g_app_info_should_show(G_APP_INFO(info))) {
        return fail(error, "应用启动器无效或当前不可显示");
    }
    struct stat st;
    if (g_lstat(source, &st) != 0 || !S_ISREG(st.st_mode)) {
        return fail(error, "应用启动器不存在或不是普通文件");
    }
    if (!g_file_test(desktop_dir, G_FILE_TEST_IS_DIR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, "桌面目录不存在");
        return FALSE;
    }
    g_autofree char *destination = desktop_shortcut_path(desktop_id, desktop_dir);
    g_autoptr(GFile) source_file = g_file_new_for_path(source);
    g_autoptr(GFile) destination_file = g_file_new_for_path(destination);
    if (!g_file_copy(source_file, destination_file, G_FILE_COPY_NONE, NULL, NULL, NULL, error)) return FALSE;
    if (g_chmod(destination, 0755) != 0) {
        int saved_errno = errno;
        g_unlink(destination);
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "无法设置快捷方式可执行权限：%s", g_strerror(saved_errno));
        return FALSE;
    }
    return TRUE;
}

gboolean ls_create_desktop_shortcut(const LsInstalledApp *app, GError **error) {
    if (!app) return fail(error, "未选择应用");
    const char *desktop_dir = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    if (!desktop_dir || !g_path_is_absolute(desktop_dir)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "系统未配置当前用户的桌面目录");
        return FALSE;
    }
    return ls_create_desktop_shortcut_in(app->path, app->desktop_id, desktop_dir, error);
}
