#define _POSIX_C_SOURCE 200809L
#include "core.h"
#include <glib/gstdio.h>
#include <unistd.h>
#include <string.h>

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
    saved(key); removed(key);
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
    int result = g_test_run();
    g_print("Isolated test fixtures: %s\n", root);
    g_free(self); g_free(root); return result;
}
