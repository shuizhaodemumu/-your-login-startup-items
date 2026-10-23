#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include <glib/gstdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

static char *root, *self;
static char *new_file(const char *name, const char *content, int mode) {
    char *path = g_build_filename(root, name, NULL);
    g_assert_true(g_file_set_contents(path, content, -1, NULL));
    g_assert_cmpint(g_chmod(path, mode), ==, 0);
    return path;
}
static char *keystr(GKeyFile *key, const char *name) { return g_key_file_get_string(key, LS_GROUP, name, NULL); }
static GKeyFile *draft(const char *path, const char *args) {
    GError *error = NULL;
    GKeyFile *key = ls_draft(path, NULL, args, NULL, FALSE, &error);
    g_assert_no_error(error); g_assert_nonnull(key);
    return key;
}
static void saved(GKeyFile *key) { GError *error = NULL; g_assert_true(ls_save(key, &error)); g_assert_no_error(error); }
static void removed(GKeyFile *key) {
    g_autofree char *id = keystr(key, LS_ID);
    GError *error = NULL; g_assert_true(ls_remove(id, &error)); g_assert_no_error(error);
}
static void no_implicit_run(void) {
    g_autofree char *marker = g_build_filename(root, "must-not-exist", NULL);
    g_autofree char *script_text = g_strdup_printf("#!/bin/sh\ntouch '%s'\n", marker);
    g_autofree char *file = new_file("never-run.sh", script_text, 0600);
    g_autoptr(GKeyFile) key = draft(file, NULL);
    saved(key); g_assert_false(g_file_test(marker, G_FILE_TEST_EXISTS)); removed(key);
}
static void duplicates(void) {
    g_autofree char *file = new_file("duplicate.txt", "document", 0600);
    g_autoptr(GKeyFile) key = draft(file, NULL); saved(key);
    g_autofree char *id = keystr(key, LS_ID);
    g_key_file_set_string(key, LS_GROUP, "Name", "updated"); saved(key);
    g_autoptr(GPtrArray) items = ls_list(NULL); g_assert_cmpuint(items->len, ==, 1);
    LsItem *read = ls_load_id(id, NULL);
    g_autofree char *name = keystr(read->key, "Name"); g_assert_cmpstr(name, ==, "updated");
    ls_item_free(read); removed(key);
}
static void argv_roundtrip(void) {
    g_autofree char *path = g_build_filename(root, "程序 = \"$;%`\\\n.exe", NULL);
    g_autofree char *binary = NULL; gsize length;
    g_assert_true(g_file_get_contents(self, &binary, &length, NULL));
    g_assert_true(g_file_set_contents(path, binary, length, NULL)); g_chmod(path, 0700);
    g_autofree char *out = g_build_filename(root, "record.ini", NULL);
    const char *expected[] = {"space argument", "$HOME; touch NEVER", "100% %f", "a\\b", "", "中文\n第二行", "a\"b'c`d"};
    g_autoptr(GString) args = g_string_new("--record ");
    g_autofree char *quoteout = g_shell_quote(out); g_string_append(args, quoteout);
    for (guint i = 0; i < G_N_ELEMENTS(expected); i++) {
        g_autofree char *quoted = g_shell_quote(expected[i]);
        g_string_append_c(args, ' '); g_string_append(args, quoted);
    }
    g_autoptr(GKeyFile) key = draft(path, args->str); saved(key);
    GError *error = NULL; g_assert_true(ls_run(key, &error)); g_assert_no_error(error);
    g_autoptr(GKeyFile) record = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(record, out, G_KEY_FILE_NONE, NULL));
    gsize count; g_auto(GStrv) actual = g_key_file_get_string_list(record, "Record", "Args", &count, NULL);
    g_assert_cmpuint(count, ==, G_N_ELEMENTS(expected));
    for (guint i = 0; i < count; i++) g_assert_cmpstr(actual[i], ==, expected[i]);
    g_autofree char *cwd = g_key_file_get_string(record, "Record", "Cwd", NULL); g_assert_cmpstr(cwd, ==, root);
    g_assert_false(g_file_test("NEVER", G_FILE_TEST_EXISTS)); removed(key);
}
static void script_without_exec(void) {
    g_autofree char *out = g_build_filename(root, "script-result", NULL);
    g_autofree char *source = new_file("nonexec.sh", "#!/bin/sh\nprintf '%s' \"$PWD\" > \"$1\"\n", 0600);
    g_autofree char *arg = g_shell_quote(out);
    g_autoptr(GKeyFile) key = draft(source, arg);
    g_assert_true(ls_run(key, NULL));
    g_autofree char *contents = NULL; g_assert_true(g_file_get_contents(out, &contents, NULL, NULL));
    g_assert_cmpstr(contents, ==, root);
}
static void desktop_preserved(void) {
    g_autofree char *source = new_file("existing.desktop", "[Desktop Entry]\nType=Application\nName=Original\nName[zh_CN]=原应用\nExec=/usr/bin/true --literal %U\nTryExec=/usr/bin/true\nIcon=utilities-terminal\nTerminal=true\nPath=/tmp\nOnlyShowIn=GNOME;\nX-Custom-Value=keep-me\n", 0600);
    g_autoptr(GKeyFile) key = draft(source, NULL);
    g_autofree char *exec = keystr(key, "Exec"), *cwd = keystr(key, "Path"), *custom = keystr(key, "X-Custom-Value");
    g_assert_cmpstr(exec, ==, "/usr/bin/true --literal %U"); g_assert_cmpstr(cwd, ==, "/tmp");
    g_assert_cmpstr(custom, ==, "keep-me");
    g_assert_true(g_key_file_get_boolean(key, LS_GROUP, "Terminal", NULL));
    g_autofree char *localized = g_key_file_get_string(key, LS_GROUP, "Name[zh_CN]", NULL); g_assert_cmpstr(localized, ==, "原应用");
    g_autoptr(GError) error = NULL;
    g_assert_true(ls_set_startup_options(key, 30, TRUE, &error));
    g_assert_no_error(error);
    saved(key);
    g_autofree char *id = keystr(key, LS_ID);
    LsItem *read = ls_load_id(id, &error);
    g_assert_no_error(error); g_assert_nonnull(read);
    g_assert_cmpint(g_key_file_get_integer(read->key, LS_GROUP, LS_DELAY, NULL), ==, 30);
    g_assert_false(g_key_file_get_boolean(read->key, LS_GROUP, "Terminal", NULL));
    g_assert_false(g_key_file_get_boolean(read->key, LS_GROUP, "StartupNotify", NULL));
    g_assert_true(ls_set_startup_options(read->key, 0, FALSE, &error));
    g_assert_no_error(error);
    g_assert_true(g_key_file_get_boolean(read->key, LS_GROUP, "Terminal", NULL));
    g_assert_false(g_key_file_has_key(read->key, LS_GROUP, "StartupNotify", NULL));
    g_assert_true(ls_save(read->key, &error));
    g_assert_no_error(error);
    ls_item_free(read);
    removed(key);
}
static void missing_and_toggle(void) {
    g_autofree char *source = new_file("will-move.txt", "temporary", 0600);
    g_autoptr(GKeyFile) key = draft(source, NULL); saved(key);
    g_autofree char *id = keystr(key, LS_ID);
    g_assert_true(ls_enable(id, FALSE, NULL));
    LsItem *read = ls_load_id(id, NULL); g_assert_true(g_key_file_get_boolean(read->key, LS_GROUP, "Hidden", NULL)); ls_item_free(read);
    g_assert_true(ls_enable(id, TRUE, NULL));
    g_unlink(source);
    g_autoptr(GError) error = NULL;
    g_assert_false(ls_validate(key, &error)); g_clear_error(&error);
    g_assert_true(ls_enable(id, FALSE, NULL));
    g_assert_false(ls_enable(id, TRUE, &error)); g_assert_nonnull(error);
    removed(key);
}
static void foreign_guard(void) {
    g_autofree char *source = new_file("foreign-source.txt", "keep", 0600);
    g_autoptr(GKeyFile) key = draft(source, NULL);
    g_autofree char *id = keystr(key, LS_ID), *dir = ls_directory(); g_mkdir_with_parents(dir, 0700);
    g_autofree char *base = g_strdup_printf(LS_PREFIX "%s.desktop", id), *path = g_build_filename(dir, base, NULL);
    const char *foreign = "[Desktop Entry]\nType=Application\nName=Not ours\nExec=/usr/bin/true\n";
    g_file_set_contents(path, foreign, -1, NULL);
    g_assert_false(ls_save(key, NULL)); g_assert_false(ls_remove(id, NULL));
    g_autofree char *data = NULL; g_file_get_contents(path, &data, NULL, NULL); g_assert_cmpstr(data, ==, foreign);
    g_autoptr(GPtrArray) list = ls_list(NULL); g_assert_cmpuint(list->len, ==, 0);
    g_unlink(path);
}
static void symlink_guard(void) {
    g_autofree char *source = new_file("link-source.txt", "source", 0600);
    g_autoptr(GKeyFile) key = draft(source, NULL);
    g_autofree char *id = keystr(key, LS_ID), *dir = ls_directory();
    g_autofree char *base = g_strdup_printf(LS_PREFIX "%s.desktop", id), *path = g_build_filename(dir, base, NULL);
    g_autofree char *victim = new_file("victim.txt", "untouched", 0600);
    g_mkdir_with_parents(dir, 0700); g_assert_cmpint(symlink(victim, path), ==, 0);
    g_assert_false(ls_save(key, NULL)); g_assert_false(ls_remove(id, NULL));
    g_autofree char *data = NULL; g_file_get_contents(victim, &data, NULL, NULL); g_assert_cmpstr(data, ==, "untouched");
    g_unlink(path);
}
static void bad_input(void) {
    g_assert_false(ls_valid_id("../../etc/passwd"));
    g_assert_null(ls_load_id("../../etc/passwd", NULL));
    g_assert_null(ls_draft("/no/such/file", NULL, NULL, NULL, FALSE, NULL));
    g_autofree char *source = new_file("bad-args.sh", "#!/bin/sh\nexit 0\n", 0600);
    g_assert_null(ls_draft(source, NULL, "'unterminated", NULL, FALSE, NULL));
    g_assert_null(ls_draft(source, NULL, NULL, "/missing/work/dir", FALSE, NULL));
    g_autofree char *doc = new_file("document.txt", "doc", 0600);
    g_assert_null(ls_draft(doc, NULL, "--not-supported", NULL, FALSE, NULL));
}
static void program_permission(void) {
    g_autofree char *path = g_build_filename(root, "not-executable.bin", NULL);
    g_autofree char *binary = NULL; gsize length; g_file_get_contents(self, &binary, &length, NULL);
    g_file_set_contents(path, binary, length, NULL); g_chmod(path, 0600);
    g_assert_null(ls_draft(path, NULL, NULL, NULL, FALSE, NULL));
}
static void missing_interpreter(void) {
    g_autofree char *source = new_file("missing-interpreter", "#!/definitely/missing/interpreter\n", 0600);
    g_assert_null(ls_draft(source, NULL, NULL, NULL, FALSE, NULL));
}
static void remove_preserves_source(void) {
    g_autofree char *source = new_file("keep-original.txt", "keep-original", 0600);
    g_autoptr(GKeyFile) key = draft(source, NULL); saved(key); removed(key);
    g_autofree char *data = NULL; g_assert_true(g_file_get_contents(source, &data, NULL, NULL)); g_assert_cmpstr(data, ==, "keep-original");
}
static void desktop_shortcut_copy(void) {
    g_autofree char *source = new_file("installed-app.desktop",
        "[Desktop Entry]\nType=Application\nName=Fixture App\nName[zh_CN]=测试应用\n"
        "Exec=/usr/bin/true --literal %U\nIcon=utilities-terminal\nTerminal=false\n",
        0644);
    g_autofree char *desktop = g_build_filename(root, "Desktop", NULL);
    g_assert_cmpint(g_mkdir_with_parents(desktop, 0700), ==, 0);
    g_autoptr(GError) error = NULL;
    g_assert_true(ls_create_desktop_shortcut_in(source, "fixture.desktop", desktop, &error));
    g_assert_no_error(error);
    g_autofree char *destination = g_build_filename(desktop, "login-startup-shortcut-fixture.desktop", NULL);
    g_assert_true(g_file_test(destination, G_FILE_TEST_IS_REGULAR));
    struct stat st;
    g_assert_cmpint(g_stat(destination, &st), ==, 0);
    g_assert_true((st.st_mode & 0111) != 0);
    g_autoptr(GKeyFile) copied = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(copied, destination, G_KEY_FILE_KEEP_TRANSLATIONS, &error));
    g_assert_no_error(error);
    g_autofree char *exec = keystr(copied, "Exec");
    g_autofree char *localized = g_key_file_get_string(copied, LS_GROUP, "Name[zh_CN]", NULL);
    g_assert_cmpstr(exec, ==, "/usr/bin/true --literal %U");
    g_assert_cmpstr(localized, ==, "测试应用");
    g_assert_false(ls_create_desktop_shortcut_in(source, "fixture.desktop", desktop, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS);
    g_clear_error(&error);
    g_autofree char *original = NULL;
    g_assert_true(g_file_get_contents(source, &original, NULL, &error));
    g_assert_no_error(error);
    g_assert_nonnull(strstr(original, "Fixture App"));
}
static void watchdog_setup_and_autostart(void) {
    g_autofree char *program = g_build_filename(root, "watch-me", NULL);
    g_autofree char *binary = NULL; gsize binary_len = 0;
    g_assert_true(g_file_get_contents(self, &binary, &binary_len, NULL));
    g_assert_true(g_file_set_contents(program, binary, binary_len, NULL));
    g_assert_cmpint(g_chmod(program, 0700), ==, 0);
    g_autoptr(GKeyFile) key = draft(program, NULL);
    g_assert_true(ls_watchdog_supported(key));
    g_key_file_set_boolean(key, LS_GROUP, LS_WATCHDOG, TRUE);
    saved(key);
    g_autoptr(GError) error = NULL;
    g_assert_true(ls_watchdog_sync_autostart(&error));
    g_assert_no_error(error);
    g_autofree char *autostart = g_build_filename(root, "config", "autostart", LS_WATCHDOG_AUTOSTART, NULL);
    g_autoptr(GKeyFile) entry = g_key_file_new();
    g_assert_true(g_key_file_load_from_file(entry, autostart, G_KEY_FILE_NONE, &error));
    g_assert_no_error(error);
    g_assert_true(g_key_file_get_boolean(entry, LS_GROUP, "X-LoginStartup-Watchdog-Owner", NULL));
    g_autofree char *exec = keystr(entry, "Exec");
    g_assert_true(g_str_has_suffix(exec, "/login-startup --watchdog"));
    g_autofree char *id = keystr(key, LS_ID);
    g_assert_true(ls_enable(id, FALSE, &error));
    g_assert_no_error(error);
    g_assert_false(g_file_test(autostart, G_FILE_TEST_EXISTS));
    removed(key);
}
static void keep_above_setup_and_autostart(void) {
    g_autofree char *program = g_build_filename(root, "pin-me", NULL);
    g_autofree char *binary = NULL; gsize binary_len = 0;
    g_assert_true(g_file_get_contents(self, &binary, &binary_len, NULL));
    g_assert_true(g_file_set_contents(program, binary, binary_len, NULL));
    g_assert_cmpint(g_chmod(program, 0700), ==, 0);
    g_autoptr(GKeyFile) key = draft(program, NULL);
    g_assert_true(ls_keep_above_supported());
    g_assert_true(ls_watchdog_supported(key));
    g_autofree char *id = keystr(key, LS_ID);
    g_key_file_set_boolean(key, LS_GROUP, "Hidden", TRUE);
    saved(key);
    g_assert_true(ls_keep_above_set(id, TRUE, NULL));
    g_autofree char *autostart = g_build_filename(root, "config", "autostart", LS_WATCHDOG_AUTOSTART, NULL);
    LsItem *inactive = ls_load_id(id, NULL);
    g_key_file_set_boolean(inactive->key, LS_GROUP, "Hidden", FALSE);
    saved(inactive->key);
    ls_item_free(inactive);
    g_autoptr(GError) error = NULL;
    g_assert_true(ls_watchdog_sync_autostart(&error));
    g_assert_no_error(error);
    g_assert_true(g_file_test(autostart, G_FILE_TEST_EXISTS));
    LsItem *loaded = ls_load_id(id, NULL);
    g_assert_nonnull(loaded);
    g_assert_true(g_key_file_get_boolean(loaded->key, LS_GROUP, LS_KEEP_ABOVE, NULL));
    ls_item_free(loaded);
    g_assert_true(ls_keep_above_set(id, FALSE, NULL));
    g_assert_false(g_file_test(autostart, G_FILE_TEST_EXISTS));
    removed(key);
}
static void watchdog_desktop_eligibility(void) {
    g_autofree char *direct = new_file("direct.desktop",
        "[Desktop Entry]\nType=Application\nName=Direct\nExec=/usr/bin/true\n", 0600);
    g_autoptr(GKeyFile) key = draft(direct, NULL);
    g_assert_true(ls_watchdog_supported(key));
    g_autofree char *wrapped = new_file("wrapped.desktop",
        "[Desktop Entry]\nType=Application\nName=Wrapped\nExec=/usr/bin/env /usr/bin/true\n", 0600);
    g_autoptr(GKeyFile) wrapped_key = draft(wrapped, NULL);
    g_assert_false(ls_watchdog_supported(wrapped_key));
    g_autofree char *script = new_file("script.sh", "#!/bin/sh\nsleep 1\n", 0700);
    g_autoptr(GKeyFile) script_key = draft(script, NULL);
    g_assert_false(ls_watchdog_supported(script_key));
}
static void watchdog_run_eligibility(void) {
    g_autofree char *run = new_file("bundle.run", "#!/bin/sh\nexec sleep 60\n", 0700);
    g_autoptr(GKeyFile) key = draft(run, NULL);
    g_assert_cmpstr(keystr(key, LS_KIND), ==, "program");
    g_assert_true(ls_watchdog_supported(key));
}
static void startup_options_roundtrip(void) {
    g_autoptr(GKeyFile) key = g_key_file_new();
    g_key_file_set_string(key, LS_GROUP, "Type", "Application");
    g_key_file_set_boolean(key, LS_GROUP, "Terminal", TRUE);
    g_key_file_set_boolean(key, LS_GROUP, "StartupNotify", TRUE);
    g_autoptr(GError) error = NULL;
    g_assert_true(ls_set_startup_options(key, 45, TRUE, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_key_file_get_integer(key, LS_GROUP, LS_DELAY, NULL), ==, 45);
    g_assert_true(g_key_file_get_boolean(key, LS_GROUP, LS_SILENT, NULL));
    g_assert_false(g_key_file_get_boolean(key, LS_GROUP, "Terminal", NULL));
    g_assert_false(g_key_file_get_boolean(key, LS_GROUP, "StartupNotify", NULL));
    g_assert_true(g_key_file_get_boolean(key, LS_GROUP, LS_SILENT_TERMINAL_VALUE, NULL));
    g_assert_true(g_key_file_get_boolean(key, LS_GROUP, LS_SILENT_NOTIFY_VALUE, NULL));
    g_assert_true(ls_set_startup_options(key, 0, FALSE, &error));
    g_assert_no_error(error);
    g_assert_cmpint(g_key_file_get_integer(key, LS_GROUP, LS_DELAY, NULL), ==, 0);
    g_assert_false(g_key_file_get_boolean(key, LS_GROUP, LS_SILENT, NULL));
    g_assert_true(g_key_file_get_boolean(key, LS_GROUP, "Terminal", NULL));
    g_assert_true(g_key_file_get_boolean(key, LS_GROUP, "StartupNotify", NULL));
    g_assert_false(g_key_file_has_key(key, LS_GROUP, LS_SILENT_TERMINAL_PRESENT, NULL));
    g_assert_false(g_key_file_has_key(key, LS_GROUP, LS_SILENT_NOTIFY_PRESENT, NULL));
    g_autofree char *serialized = g_key_file_to_data(key, NULL, NULL);
    g_assert_nonnull(strstr(serialized, "X-GNOME-Autostart-Delay=0"));
    g_assert_false(ls_set_startup_options(key, 3601, FALSE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error(&error);

    g_autoptr(GKeyFile) absent = g_key_file_new();
    g_key_file_set_string(absent, LS_GROUP, "Type", "Application");
    g_assert_true(ls_set_startup_options(absent, 3600, TRUE, &error));
    g_assert_no_error(error);
    g_assert_true(ls_set_startup_options(absent, 0, FALSE, &error));
    g_assert_no_error(error);
    g_assert_false(g_key_file_has_key(absent, LS_GROUP, "Terminal", NULL));
    g_assert_false(g_key_file_has_key(absent, LS_GROUP, "StartupNotify", NULL));
}
static void watchdog_duplicate_target_rejected(void) {
    g_autofree char *program = g_build_filename(root, "same-app", NULL);
    g_autofree char *binary = NULL; gsize binary_len = 0;
    g_assert_true(g_file_get_contents(self, &binary, &binary_len, NULL));
    g_assert_true(g_file_set_contents(program, binary, binary_len, NULL));
    g_assert_cmpint(g_chmod(program, 0700), ==, 0);
    g_autoptr(GKeyFile) direct = draft(program, NULL);
    g_key_file_set_boolean(direct, LS_GROUP, LS_WATCHDOG, TRUE);
    saved(direct);
    g_autofree char *desktop_path = g_build_filename(root, "same-app.desktop", NULL);
    g_autofree char *desktop_text = g_strdup_printf("[Desktop Entry]\nType=Application\nName=Same app\nExec=%s\n", program);
    g_assert_true(g_file_set_contents(desktop_path, desktop_text, -1, NULL));
    g_autoptr(GKeyFile) desktop = draft(desktop_path, NULL);
    saved(desktop);
    g_autofree char *desktop_id = keystr(desktop, LS_ID);
    g_autoptr(GError) error = NULL;
    g_assert_false(ls_watchdog_set(desktop_id, TRUE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_nonnull(strstr(error->message, "另一个看门狗"));
    removed(direct); removed(desktop);
}
int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--record")) {
        g_autoptr(GKeyFile) key = g_key_file_new();
        g_key_file_set_string_list(key, "Record", "Args", (const char * const *)argv + 3, argc - 3);
        g_autofree char *cwd = g_get_current_dir(); g_key_file_set_string(key, "Record", "Cwd", cwd);
        return g_key_file_save_to_file(key, argv[2], NULL) ? 0 : 1;
    }
    root = g_dir_make_tmp("login-startup-test-XXXXXX", NULL); g_assert_nonnull(root);
    g_autofree char *config = g_build_filename(root, "config", NULL); g_setenv("XDG_CONFIG_HOME", config, TRUE);
    self = g_file_read_link("/proc/self/exe", NULL);
    g_autofree char *old_path = g_strdup(g_getenv("PATH"));
    g_autofree char *fake_binary = g_build_filename(root, "login-startup", NULL);
    g_autofree char *binary = NULL; gsize binary_len = 0;
    g_assert_true(g_file_get_contents(self, &binary, &binary_len, NULL));
    g_assert_true(g_file_set_contents(fake_binary, binary, binary_len, NULL));
    g_assert_cmpint(g_chmod(fake_binary, 0700), ==, 0);
    g_autofree char *test_path = g_strdup_printf("%s:%s", root, old_path ? old_path : "");
    g_setenv("PATH", test_path, TRUE);
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/startup/no-implicit-run", no_implicit_run);
    g_test_add_func("/startup/duplicates", duplicates);
    g_test_add_func("/startup/argv-roundtrip", argv_roundtrip);
    g_test_add_func("/startup/script-without-exec", script_without_exec);
    g_test_add_func("/startup/desktop-preserved", desktop_preserved);
    g_test_add_func("/startup/missing-toggle", missing_and_toggle);
    g_test_add_func("/startup/foreign-guard", foreign_guard);
    g_test_add_func("/startup/symlink-guard", symlink_guard);
    g_test_add_func("/startup/bad-input", bad_input);
    g_test_add_func("/startup/program-permission", program_permission);
    g_test_add_func("/startup/missing-interpreter", missing_interpreter);
    g_test_add_func("/startup/remove-preserves-source", remove_preserves_source);
    g_test_add_func("/startup/desktop-shortcut-copy", desktop_shortcut_copy);
    g_test_add_func("/startup/watchdog-setup-and-autostart", watchdog_setup_and_autostart);
    g_test_add_func("/startup/keep-above-setup-and-autostart", keep_above_setup_and_autostart);
    g_test_add_func("/startup/watchdog-desktop-eligibility", watchdog_desktop_eligibility);
    g_test_add_func("/startup/watchdog-run-eligibility", watchdog_run_eligibility);
    g_test_add_func("/startup/startup-options-roundtrip", startup_options_roundtrip);
    g_test_add_func("/startup/watchdog-duplicate-target-rejected", watchdog_duplicate_target_rejected);
    int result = g_test_run();
    g_print("Isolated test fixtures: %s\n", root);
    if (old_path) g_setenv("PATH", old_path, TRUE); else g_unsetenv("PATH");
    g_free(self); g_free(root); return result;
}
