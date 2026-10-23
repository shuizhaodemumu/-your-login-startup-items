#include "core.h"
#include <adwaita.h>
#include <string.h>

typedef struct {
    AdwApplication *app;
    GtkWindow *window;
    AdwToastOverlay *overlay;
    GtkEntry *source, *name, *cwd, *args;
    GtkCheckButton *terminal;
    GtkCheckButton *watchdog;
    GtkLabel *type_hint, *count, *helper_status;
    GtkButton *save;
    GtkListBox *list;
    GtkWidget *empty;
    GtkWindow *shortcut_window;
    GtkListBox *shortcut_list;
    GtkSearchEntry *shortcut_search;
    GtkLabel *shortcut_summary;
    GtkButton *shortcut_create;
    GPtrArray *installed_apps;
    AdwToast *last_toast;
    char *editing_id;
    GPtrArray *items;
} Ui;
typedef struct { Ui *ui; LsItem *item; } Row;
typedef struct { LsInstalledApp *app; GtkCheckButton *check; GtkLabel *status; } ShortcutRow;

static void refresh(Ui *ui);
static void open_shortcut_picker(GtkButton *button, gpointer data);
static gboolean update_helper_status(gpointer data);
static void toast(Ui *ui, const char *message) {
    if (ui->last_toast) {
        AdwToast *previous = ui->last_toast;
        g_object_remove_weak_pointer(G_OBJECT(previous), (gpointer *)&ui->last_toast);
        ui->last_toast = NULL;
        adw_toast_dismiss(previous);
    }
    ui->last_toast = adw_toast_new(message);
    adw_toast_set_use_markup(ui->last_toast, FALSE);
    g_object_add_weak_pointer(G_OBJECT(ui->last_toast), (gpointer *)&ui->last_toast);
    adw_toast_overlay_add_toast(ui->overlay, ui->last_toast);
}
static GtkWidget *text(const char *label, const char *css) {
    GtkWidget *widget = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_label_set_wrap(GTK_LABEL(widget), TRUE);
    if (css) gtk_widget_add_css_class(widget, css);
    return widget;
}
static GtkWidget *icon_button(const char *icon, const char *label, GCallback callback, gpointer data) {
    GtkWidget *button = gtk_button_new_from_icon_name(icon);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_set_tooltip_text(button, label);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
    g_signal_connect(button, "clicked", callback, data);
    return button;
}
static GtkEntry *field(GtkBox *box, const char *label, const char *placeholder) {
    gtk_box_append(box, text(label, "caption-heading"));
    GtkEntry *entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(entry, placeholder);
    gtk_accessible_update_property(GTK_ACCESSIBLE(entry), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
    gtk_box_append(box, GTK_WIDGET(entry));
    return entry;
}
static void clear_form(Ui *ui) {
    g_clear_pointer(&ui->editing_id, g_free);
    GtkEntry *entries[] = { ui->source, ui->name, ui->cwd, ui->args };
    for (guint i = 0; i < G_N_ELEMENTS(entries); ++i) gtk_editable_set_text(GTK_EDITABLE(entries[i]), "");
    gtk_check_button_set_active(ui->terminal, FALSE);
    gtk_check_button_set_active(ui->watchdog, FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->args), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->terminal), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->watchdog), FALSE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(ui->watchdog), "支持普通可执行程序、可识别进程的应用快捷方式，以及可执行的 .run 启动器");
    gtk_label_set_text(ui->type_hint, "登录桌面后启动，不会在添加时运行文件。");
    gtk_button_set_label(ui->save, "添加自启动");
}
static void fill(Ui *ui, GKeyFile *key) {
    const char *keys[] = { LS_SOURCE, "Name", "Path" };
    GtkEntry *entries[] = { ui->source, ui->name, ui->cwd };
    for (guint i = 0; i < 3; ++i) {
        g_autofree char *str = g_key_file_get_string(key, LS_GROUP, keys[i], NULL);
        gtk_editable_set_text(GTK_EDITABLE(entries[i]), str ? str : "");
    }
    gsize count = 0;
    g_auto(GStrv) argv = g_key_file_get_string_list(key, LS_GROUP, LS_ARGS, &count, NULL);
    g_autoptr(GString) args = g_string_new(NULL);
    for (gsize i = 0; i < count; ++i) {
        g_autofree char *quoted = g_shell_quote(argv[i]);
        if (i) g_string_append_c(args, ' ');
        g_string_append(args, quoted);
    }
    gtk_editable_set_text(GTK_EDITABLE(ui->args), args->str);
    gtk_check_button_set_active(ui->terminal, g_key_file_get_boolean(key, LS_GROUP, "Terminal", NULL));
    g_autofree char *kind = g_key_file_get_string(key, LS_GROUP, LS_KIND, NULL);
    gboolean desktop = !g_strcmp0(kind, "desktop");
    gboolean watchdog_supported = ls_watchdog_supported(key);
    gtk_check_button_set_active(ui->watchdog, watchdog_supported && g_key_file_get_boolean(key, LS_GROUP, LS_WATCHDOG, NULL));
    gboolean auto_start_enabled = !g_key_file_get_boolean(key, LS_GROUP, "Hidden", NULL);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->watchdog), watchdog_supported && auto_start_enabled);
    gtk_widget_set_tooltip_text(GTK_WIDGET(ui->watchdog), !watchdog_supported ? "支持普通可执行程序、可识别进程的应用快捷方式，以及有执行权限的 .run 启动器" : auto_start_enabled ? "进程退出后由后台助手重新启动" : "先启用登录自启动，再启用看门狗");
    gtk_widget_set_sensitive(GTK_WIDGET(ui->args), !desktop);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->terminal), !desktop);
    gtk_label_set_text(ui->type_hint, desktop ? "应用快捷方式：保留原命令、参数、图标与终端设置。" : !g_strcmp0(kind, "script") ? "脚本：识别解释器；添加不会执行脚本。" : !g_strcmp0(kind, "document") ? "普通文档：登录后交给默认应用打开。" : "可执行程序：登录后按指定参数启动。");
}
static void select_path(Ui *ui, const char *path) {
    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) draft = ls_draft(path, NULL, NULL, NULL, FALSE, &error);
    if (!draft) { toast(ui, error->message); return; }
    fill(ui, draft);
}
static void file_selected(GObject *object, GAsyncResult *result, gpointer data) {
    Ui *ui = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(object), result, &error);
    if (!file) {
        if (error && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) toast(ui, error->message);
        return;
    }
    g_autofree char *path = g_file_get_path(file);
    if (path) select_path(ui, path); else toast(ui, "请选择本机上的文件");
}
static void choose_file(GtkButton *button, gpointer data) {
    (void)button;
    Ui *ui = data;
    g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "选择登录后要启动的文件");
    gtk_file_dialog_open(dialog, ui->window, NULL, file_selected, ui);
}
static gboolean dropped(GtkDropTarget *target, const GValue *value, double x, double y, gpointer data) {
    (void)target; (void)x; (void)y;
    Ui *ui = data;
    GFile *file = NULL;
    if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
        GSList *files = gdk_file_list_get_files(g_value_get_boxed(value));
        if (!files || files->next) { toast(ui, "请一次拖入一个文件"); return FALSE; }
        file = files->data;
    } else if (G_VALUE_HOLDS(value, G_TYPE_FILE)) file = g_value_get_object(value);
    if (!file) return FALSE;
    g_autofree char *path = g_file_get_path(file);
    if (!path) { toast(ui, "暂不支持远程文件"); return FALSE; }
    select_path(ui, path);
    return TRUE;
}
static void save_item(GtkButton *button, gpointer data) {
    (void)button;
    Ui *ui = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) key = ls_draft(gtk_editable_get_text(GTK_EDITABLE(ui->source)), gtk_editable_get_text(GTK_EDITABLE(ui->name)), gtk_editable_get_text(GTK_EDITABLE(ui->args)), gtk_editable_get_text(GTK_EDITABLE(ui->cwd)), gtk_check_button_get_active(ui->terminal), &error);
    if (!key) { toast(ui, error->message); return; }
    g_autofree char *id = g_key_file_get_string(key, LS_GROUP, LS_ID, NULL);
    gboolean watchdog_requested = gtk_check_button_get_active(ui->watchdog);
    LsItem *old = ls_load_id(id, NULL);
    gboolean updated = old != NULL;
    if (old) {
        gboolean hidden = g_key_file_get_boolean(old->key, LS_GROUP, "Hidden", NULL);
        g_key_file_set_boolean(key, LS_GROUP, "Hidden", hidden);
        g_key_file_set_boolean(key, LS_GROUP, "X-GNOME-Autostart-enabled", !hidden);
        ls_item_free(old);
    }
    /* Enable through ls_watchdog_set only after saving the base item; this keeps duplicate-watch rejection atomic. */
    gboolean hidden = g_key_file_get_boolean(key, LS_GROUP, "Hidden", NULL);
    g_key_file_set_boolean(key, LS_GROUP, LS_WATCHDOG, watchdog_requested && hidden);
    if (!ls_save(key, &error)) { toast(ui, error->message); return; }
    if (watchdog_requested && !hidden) {
        if (!ls_watchdog_set(id, TRUE, &error)) { toast(ui, error->message); refresh(ui); return; }
    } else if (!ls_watchdog_sync_autostart(&error)) { toast(ui, error->message); refresh(ui); return; }
    if (ui->editing_id && strcmp(ui->editing_id, id) && !ls_remove(ui->editing_id, &error)) {
        toast(ui, "新路径已保存，但旧启动项未能移除，请检查列表");
    } else toast(ui, updated ? "已更新启动配置，未运行文件" : "已添加，下次登录桌面时启动");
    clear_form(ui); refresh(ui);
}
static gboolean refresh_idle(gpointer data) { refresh(data); return G_SOURCE_REMOVE; }
static gboolean switched(GtkSwitch *widget, gboolean enabled, gpointer data) {
    (void)widget;
    Row *row = data;
    g_autofree char *id = g_key_file_get_string(row->item->key, LS_GROUP, LS_ID, NULL);
    g_autoptr(GError) error = NULL;
    if (!ls_enable(id, enabled, &error)) { toast(row->ui, error->message); return TRUE; }
    toast(row->ui, enabled ? "已启用，下次登录时启动" : "已禁用，原文件保持不变");
    g_idle_add(refresh_idle, row->ui);
    return FALSE;
}
static gboolean watchdog_switched(GtkSwitch *widget, gboolean enabled, gpointer data) {
    (void)widget;
    Row *row = data;
    g_autofree char *id = g_key_file_get_string(row->item->key, LS_GROUP, LS_ID, NULL);
    g_autoptr(GError) error = NULL;
    if (!ls_watchdog_set(id, enabled, &error)) { toast(row->ui, error->message); return TRUE; }
    toast(row->ui, enabled ? "看门狗已启用，后台助手已配置为登录时启动" : "看门狗已关闭");
    g_idle_add(refresh_idle, row->ui);
    return FALSE;
}
static void trial_finished(GObject *object, GAsyncResult *result, gpointer data) {
    Ui *ui = data;
    g_autoptr(GError) error = NULL;
    if (!g_subprocess_wait_check_finish(G_SUBPROCESS(object), result, &error)) {
        g_autofree char *message = g_strdup_printf("试运行进程未正常结束：%s；可使用终端模式查看输出", error->message);
        toast(ui, message);
    } else toast(ui, "试运行进程已结束（退出码 0），请确认应用的实际状态");
    g_object_unref(object);
}
static void try_item(GtkButton *button, gpointer data) {
    (void)button;
    Row *row = data;
    g_autoptr(GError) error = NULL;
    g_autofree char *kind = g_key_file_get_string(row->item->key, LS_GROUP, LS_KIND, NULL);
    if (g_strcmp0(kind, "desktop") && !g_key_file_get_boolean(row->item->key, LS_GROUP, "Terminal", NULL)) {
        GSubprocess *process = ls_spawn(row->item->key, &error);
        if (process) {
            toast(row->ui, "已发起启动，请观察应用窗口");
            g_subprocess_wait_check_async(process, NULL, trial_finished, row->ui);
        } else toast(row->ui, error->message);
    } else if (ls_trial(row->item->key, &error)) toast(row->ui, "已发起启动，请观察应用窗口或终端");
    else toast(row->ui, error->message);
}
static void edit_item(GtkButton *button, gpointer data) {
    (void)button;
    Row *row = data;
    g_free(row->ui->editing_id);
    row->ui->editing_id = g_key_file_get_string(row->item->key, LS_GROUP, LS_ID, NULL);
    fill(row->ui, row->item->key);
    gtk_button_set_label(row->ui->save, "保存修改");
    gtk_widget_grab_focus(GTK_WIDGET(row->ui->name));
}
static void delete_response(AdwAlertDialog *dialog, const char *response, gpointer data) {
    Ui *ui = data;
    if (strcmp(response, "delete")) return;
    const char *id = g_object_get_data(G_OBJECT(dialog), "item-id");
    g_autoptr(GError) error = NULL;
    if (ls_remove(id, &error)) {
        if (!g_strcmp0(ui->editing_id, id)) clear_form(ui);
        toast(ui, "启动配置已删除，原文件保持不变"); refresh(ui);
    } else toast(ui, error->message);
}
static void delete_item(GtkButton *button, gpointer data) {
    (void)button;
    Row *row = data;
    AdwDialog *dialog = adw_alert_dialog_new("删除这条启动配置？", "只删除登录自启动配置，不会删除原程序、脚本或文档。");
    char *id = g_key_file_get_string(row->item->key, LS_GROUP, LS_ID, NULL);
    g_object_set_data_full(G_OBJECT(dialog), "item-id", id, g_free);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "cancel", "取消", "delete", "删除配置", NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "delete", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(delete_response), row->ui);
    adw_dialog_present(dialog, GTK_WIDGET(row->ui->window));
}
static void refresh(Ui *ui) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ui->list)))) gtk_list_box_remove(ui->list, child);
    g_clear_pointer(&ui->items, g_ptr_array_unref);
    g_autoptr(GError) error = NULL;
    ui->items = ls_list(&error);
    if (!ui->items) { toast(ui, error->message); return; }
    guint enabled = 0, watched = 0;
    for (guint i = 0; i < ui->items->len; ++i) {
        LsItem *item = g_ptr_array_index(ui->items, i);
        GtkWidget *widget = adw_action_row_new();
        adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(widget), FALSE);
        g_autofree char *name = g_key_file_get_string(item->key, LS_GROUP, "Name", NULL);
        g_autofree char *path = g_key_file_get_string(item->key, LS_GROUP, LS_SOURCE, NULL);
        gboolean active = !g_key_file_get_boolean(item->key, LS_GROUP, "Hidden", NULL);
        gboolean watchdog_enabled = g_key_file_get_boolean(item->key, LS_GROUP, LS_WATCHDOG, NULL);
        if (active) enabled++;
        if (active && watchdog_enabled && ls_watchdog_supported(item->key)) watched++;
        g_autoptr(GError) validation = NULL;
        gboolean valid = ls_validate(item->key, &validation);
        const char *watchdog_state = watchdog_enabled ? (active ? "看门狗已启用" : "看门狗已暂停") : "看门狗关闭";
        g_autofree char *subtitle = g_strdup_printf("%s · %s · %s", valid ? (active ? "已启用" : "已禁用") : "失效，请编辑修复", watchdog_state, path);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widget), name ? name : "未命名");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(widget), subtitle);
        adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(widget), 2);
        if (!valid) gtk_widget_set_tooltip_text(widget, validation->message);
        g_autofree char *icon_name = g_key_file_get_string(item->key, LS_GROUP, "Icon", NULL);
        GtkWidget *icon;
        if (icon_name && g_path_is_absolute(icon_name)) icon = gtk_image_new_from_file(icon_name);
        else icon = gtk_image_new_from_icon_name(icon_name ? icon_name : "application-x-executable");
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 28);
        adw_action_row_add_prefix(ADW_ACTION_ROW(widget), icon);
        Row *row = g_new0(Row, 1); row->ui = ui; row->item = item;
        g_object_set_data_full(G_OBJECT(widget), "row", row, g_free);
        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_widget_set_valign(actions, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(actions), icon_button("media-playback-start-symbolic", "立即试运行", G_CALLBACK(try_item), row));
        gtk_box_append(GTK_BOX(actions), icon_button("document-edit-symbolic", "编辑", G_CALLBACK(edit_item), row));
        gtk_box_append(GTK_BOX(actions), icon_button("user-trash-symbolic", "删除配置", G_CALLBACK(delete_item), row));
        GtkWidget *toggle = gtk_switch_new();
        gtk_widget_set_valign(toggle, GTK_ALIGN_CENTER);
        gtk_switch_set_active(GTK_SWITCH(toggle), active);
        gtk_widget_set_tooltip_text(toggle, "登录后自动启动");
        gtk_accessible_update_property(GTK_ACCESSIBLE(toggle), GTK_ACCESSIBLE_PROPERTY_LABEL, "登录后自动启动", -1);
        gtk_widget_set_sensitive(toggle, valid);
        g_signal_connect(toggle, "state-set", G_CALLBACK(switched), row);
        gtk_box_append(GTK_BOX(actions), toggle);
        GtkWidget *watchdog_toggle = gtk_switch_new();
        gtk_widget_set_valign(watchdog_toggle, GTK_ALIGN_CENTER);
        gboolean watchdog_supported = ls_watchdog_supported(item->key);
        gtk_switch_set_active(GTK_SWITCH(watchdog_toggle), watchdog_enabled);
        gtk_widget_set_sensitive(watchdog_toggle, valid && active && watchdog_supported);
        gtk_widget_set_tooltip_text(watchdog_toggle, watchdog_supported ? "程序退出后自动重启" : "支持直接启动程序、可识别进程的应用快捷方式和可执行的 .run 启动器");
        gtk_accessible_update_property(GTK_ACCESSIBLE(watchdog_toggle), GTK_ACCESSIBLE_PROPERTY_LABEL, "程序退出后自动重启（看门狗）", -1);
        g_signal_connect(watchdog_toggle, "state-set", G_CALLBACK(watchdog_switched), row);
        gtk_box_append(GTK_BOX(actions), watchdog_toggle);
        adw_action_row_add_suffix(ADW_ACTION_ROW(widget), actions);
        gtk_list_box_append(ui->list, widget);
    }
    g_autofree char *summary = g_strdup_printf("%u 个启动项 · %u 个已启用 · %u 个看门狗", ui->items->len, enabled, watched);
    gtk_label_set_text(ui->count, summary);
    update_helper_status(ui);
    gtk_widget_set_visible(ui->empty, ui->items->len == 0);
    gtk_widget_set_visible(GTK_WIDGET(ui->list), ui->items->len > 0);
}
static void shortcut_row_free(ShortcutRow *row) { g_free(row); }
static void update_shortcut_selection(Ui *ui) {
    if (!ui->shortcut_list) return;
    guint selected = 0, available = 0;
    for (int i = 0; ; ++i) {
        GtkListBoxRow *widget = gtk_list_box_get_row_at_index(ui->shortcut_list, i);
        if (!widget) break;
        ShortcutRow *row = g_object_get_data(G_OBJECT(widget), "shortcut-row");
        if (!row) continue;
        if (!row->app->shortcut_exists) {
            available++;
            if (gtk_check_button_get_active(row->check)) selected++;
        }
    }
    g_autofree char *summary = g_strdup_printf("已选 %u 个 / 可创建 %u 个", selected, available);
    gtk_label_set_text(ui->shortcut_summary, summary);
    g_autofree char *label = g_strdup_printf("创建所选桌面快捷方式（%u）", selected);
    gtk_button_set_label(ui->shortcut_create, label);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->shortcut_create), selected > 0);
}
static gboolean shortcut_filter(GtkListBoxRow *widget, gpointer data) {
    Ui *ui = data;
    ShortcutRow *row = g_object_get_data(G_OBJECT(widget), "shortcut-row");
    const char *query = gtk_editable_get_text(GTK_EDITABLE(ui->shortcut_search));
    if (!query || !*query) return TRUE;
    g_autofree char *haystack = g_strdup_printf("%s %s %s", row->app->name, row->app->package, row->app->desktop_id);
    return g_str_match_string(query, haystack, TRUE);
}
static void shortcut_search_changed(GtkEditable *entry, gpointer data) {
    (void)entry;
    Ui *ui = data;
    gtk_list_box_invalidate_filter(ui->shortcut_list);
}
static void shortcut_check_toggled(GtkCheckButton *button, gpointer data) {
    (void)button;
    update_shortcut_selection(data);
}
static void populate_shortcut_rows(Ui *ui, GPtrArray *apps) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(ui->shortcut_list))))
        gtk_list_box_remove(ui->shortcut_list, child);
    g_clear_pointer(&ui->installed_apps, g_ptr_array_unref);
    ui->installed_apps = apps;
    for (guint i = 0; i < apps->len; ++i) {
        LsInstalledApp *app = g_ptr_array_index(apps, i);
        GtkWidget *list_row = gtk_list_box_row_new();
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(line, 12); gtk_widget_set_margin_end(line, 12);
        gtk_widget_set_margin_top(line, 8); gtk_widget_set_margin_bottom(line, 8);
        GtkCheckButton *check = GTK_CHECK_BUTTON(gtk_check_button_new());
        gtk_accessible_update_property(GTK_ACCESSIBLE(check), GTK_ACCESSIBLE_PROPERTY_LABEL, app->name, -1);
        gtk_box_append(GTK_BOX(line), GTK_WIDGET(check));
        GtkWidget *icon = app->icon ? gtk_image_new_from_gicon(app->icon) : gtk_image_new_from_icon_name("application-x-executable");
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
        gtk_box_append(GTK_BOX(line), icon);
        GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_widget_set_hexpand(labels, TRUE);
        GtkWidget *title = gtk_label_new(app->name);
        gtk_label_set_xalign(GTK_LABEL(title), 0);
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(title, "heading");
        gtk_box_append(GTK_BOX(labels), title);
        GtkWidget *subtitle = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(subtitle), 0);
        gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_add_css_class(subtitle, "dim-label");
        gtk_box_append(GTK_BOX(labels), subtitle);
        gtk_box_append(GTK_BOX(line), labels);
        ShortcutRow *row = g_new0(ShortcutRow, 1);
        row->app = app;
        row->check = check;
        row->status = GTK_LABEL(subtitle);
        g_object_set_data_full(G_OBJECT(list_row), "shortcut-row", row, (GDestroyNotify)shortcut_row_free);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(list_row), line);
        if (app->shortcut_exists) {
            gtk_label_set_text(row->status, app->package);
            gtk_check_button_set_active(check, FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(check), FALSE);
            gtk_widget_set_tooltip_text(list_row, "已由登录自启助手创建桌面快捷方式");
        } else {
            g_autofree char *status = g_strdup_printf("%s · deb 包", app->package);
            gtk_label_set_text(row->status, status);
            gtk_check_button_set_active(check, FALSE);
            g_signal_connect(check, "toggled", G_CALLBACK(shortcut_check_toggled), ui);
        }
        gtk_list_box_append(ui->shortcut_list, list_row);
    }
    gtk_list_box_invalidate_filter(ui->shortcut_list);
    update_shortcut_selection(ui);
}
static void create_selected_shortcuts(GtkButton *button, gpointer data) {
    (void)button;
    Ui *ui = data;
    guint created = 0, failed = 0;
    g_autofree char *first_error = NULL;
    for (int i = 0; ; ++i) {
        GtkListBoxRow *widget = gtk_list_box_get_row_at_index(ui->shortcut_list, i);
        if (!widget) break;
        ShortcutRow *row = g_object_get_data(G_OBJECT(widget), "shortcut-row");
        if (!row || row->app->shortcut_exists || !gtk_check_button_get_active(row->check)) continue;
        g_autoptr(GError) error = NULL;
        if (ls_create_desktop_shortcut(row->app, &error)) {
            created++;
            row->app->shortcut_exists = TRUE;
            gtk_check_button_set_active(row->check, FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(row->check), FALSE);
            gtk_label_set_text(row->status, row->app->package);
            gtk_widget_set_tooltip_text(GTK_WIDGET(widget), "已由登录自启助手创建桌面快捷方式");
        } else {
            failed++;
            if (!first_error) first_error = g_strdup(error->message);
        }
    }
    update_shortcut_selection(ui);
    if (created && failed) {
        g_autofree char *message = g_strdup_printf("已创建 %u 个；%u 个未能创建：%s", created, failed, first_error);
        toast(ui, message);
    } else if (created) {
        g_autofree char *message = g_strdup_printf("已在桌面创建 %u 个应用快捷方式", created);
        toast(ui, message);
    } else if (failed) {
        toast(ui, first_error);
    }
}
static void close_shortcut_picker(GtkButton *button, gpointer data) {
    (void)button;
    Ui *ui = data;
    if (ui->shortcut_window) gtk_window_close(ui->shortcut_window);
}
static void installed_apps_scanned(GObject *source, GAsyncResult *result, gpointer data) {
    (void)source;
    Ui *ui = data;
    g_autoptr(GError) error = NULL;
    GPtrArray *apps = g_task_propagate_pointer(G_TASK(result), &error);
    if (!ui->shortcut_window) {
        g_clear_pointer(&apps, g_ptr_array_unref);
        return;
    }
    if (!apps) {
        gtk_label_set_text(ui->shortcut_summary, error ? error->message : "扫描应用失败");
        return;
    }
    populate_shortcut_rows(ui, apps);
    if (!apps->len) gtk_label_set_text(ui->shortcut_summary, "没有找到由已安装 deb 包提供的可启动图形应用");
}
static void scan_installed_apps(GTask *task, gpointer source, gpointer task_data, GCancellable *cancellable) {
    (void)source; (void)task_data; (void)cancellable;
    GError *error = NULL;
    GPtrArray *apps = ls_installed_apps(&error);
    if (error) g_task_return_error(task, error);
    else g_task_return_pointer(task, apps, (GDestroyNotify)g_ptr_array_unref);
}
static void open_shortcut_picker(GtkButton *button, gpointer data) {
    (void)button;
    Ui *ui = data;
    if (ui->shortcut_window) { gtk_window_present(ui->shortcut_window); return; }
    GtkWindow *window = GTK_WINDOW(gtk_window_new());
    ui->shortcut_window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&ui->shortcut_window);
    gtk_window_set_title(window, "已安装的 deb 应用");
    gtk_window_set_default_size(window, 680, 700);
    gtk_window_set_transient_for(window, ui->window);
    gtk_window_set_modal(window, TRUE);
    gtk_window_set_destroy_with_parent(window, TRUE);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), adw_window_title_new("已安装的 deb 应用", "为图形应用创建桌面快捷方式"));
    gtk_window_set_titlebar(window, header);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 20); gtk_widget_set_margin_end(content, 20);
    gtk_widget_set_margin_top(content, 16); gtk_widget_set_margin_bottom(content, 16);
    gtk_box_append(GTK_BOX(outer), content);
    ui->shortcut_search = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    gtk_accessible_update_property(GTK_ACCESSIBLE(ui->shortcut_search), GTK_ACCESSIBLE_PROPERTY_LABEL, "搜索已安装的应用", -1);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(ui->shortcut_search));
    GtkWidget *note = text("默认不勾选任何应用，请自行选择要创建桌面快捷方式的项目。扫描会排除库、命令行工具和没有桌面启动器的 deb 包。", "dim-label");
    gtk_box_append(GTK_BOX(content), note);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    ui->shortcut_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(ui->shortcut_list, GTK_SELECTION_NONE);
    gtk_widget_add_css_class(GTK_WIDGET(ui->shortcut_list), "boxed-list");
    gtk_list_box_set_filter_func(ui->shortcut_list, shortcut_filter, ui, NULL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(ui->shortcut_list));
    gtk_box_append(GTK_BOX(content), scroll);
    ui->shortcut_summary = GTK_LABEL(text("正在扫描可启动的 deb 应用…", "dim-label"));
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(ui->shortcut_summary));
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    GtkWidget *close = gtk_button_new_with_label("关闭");
    g_signal_connect(close, "clicked", G_CALLBACK(close_shortcut_picker), ui);
    gtk_box_append(GTK_BOX(actions), close);
    ui->shortcut_create = GTK_BUTTON(gtk_button_new_with_label("创建所选桌面快捷方式"));
    gtk_widget_add_css_class(GTK_WIDGET(ui->shortcut_create), "suggested-action");
    g_signal_connect(ui->shortcut_create, "clicked", G_CALLBACK(create_selected_shortcuts), ui);
    gtk_widget_set_sensitive(GTK_WIDGET(ui->shortcut_create), FALSE);
    gtk_box_append(GTK_BOX(actions), GTK_WIDGET(ui->shortcut_create));
    gtk_box_append(GTK_BOX(content), actions);
    gtk_window_set_child(window, outer);
    g_signal_connect(ui->shortcut_search, "search-changed", G_CALLBACK(shortcut_search_changed), ui);
    gtk_window_present(window);
    GTask *task = g_task_new(ui->app, NULL, installed_apps_scanned, ui);
    g_task_run_in_thread(task, scan_installed_apps);
    g_object_unref(task);
}
static void reset_clicked(GtkButton *button, gpointer data) { (void)button; clear_form(data); }
static void reload_clicked(GtkButton *button, gpointer data) { (void)button; refresh(data); }
static gboolean update_helper_status(gpointer data) {
    Ui *ui = data;
    if (!ui->helper_status) return G_SOURCE_CONTINUE;
    gboolean autostart = ls_watchdog_autostart_enabled();
    gboolean running = ls_watchdog_is_running();
    g_autofree char *status = g_strdup_printf("后台助手：%s · 登录时启动：%s", running ? "正在运行" : "未运行", autostart ? "已配置" : "未配置");
    gtk_label_set_text(ui->helper_status, status);
    return G_SOURCE_CONTINUE;
}
static void activate(GApplication *application, gpointer data) {
    Ui *ui = data;
    if (ui->window) { gtk_window_present(ui->window); return; }
    ui->window = GTK_WINDOW(adw_application_window_new(GTK_APPLICATION(application)));
    gtk_window_set_title(ui->window, "登录自启助手");
    gtk_window_set_default_size(ui->window, 1120, 740);
    GtkWidget *toolbar = adw_toolbar_view_new();
    GtkWidget *header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), adw_window_title_new("登录自启助手", "让常用文件，准时就位"));
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), icon_button("view-refresh-symbolic", "刷新启动项列表", G_CALLBACK(reload_clicked), ui));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    ui->overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(ui->overlay));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(ui->window), toolbar);
    GtkWidget *outer_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(outer_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    adw_toast_overlay_set_child(ui->overlay, outer_scroll);
    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 26);
    gtk_widget_set_margin_start(layout, 28); gtk_widget_set_margin_end(layout, 28);
    gtk_widget_set_margin_top(layout, 20); gtk_widget_set_margin_bottom(layout, 24);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(outer_scroll), layout);
    GtkWidget *form = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(form, 330, -1);
    gtk_widget_set_valign(form, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(layout), form);
    gtk_box_append(GTK_BOX(form), text("添加一个启动项", "title-2"));
    GtkWidget *drop = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(drop, "drop-zone");
    GtkWidget *drop_icon = gtk_image_new_from_icon_name("document-send-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(drop_icon), 38);
    gtk_box_append(GTK_BOX(drop), drop_icon);
    GtkWidget *drop_title = gtk_label_new("把文件拖到这里");
    gtk_widget_add_css_class(drop_title, "heading");
    gtk_box_append(GTK_BOX(drop), drop_title);
    GtkWidget *choose = gtk_button_new_with_label("选择文件");
    gtk_widget_set_halign(choose, GTK_ALIGN_CENTER);
    g_signal_connect(choose, "clicked", G_CALLBACK(choose_file), ui);
    gtk_box_append(GTK_BOX(drop), choose);
    gtk_box_append(GTK_BOX(form), drop);
    GtkDropTarget *target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY);
    GType types[] = { GDK_TYPE_FILE_LIST, G_TYPE_FILE };
    gtk_drop_target_set_gtypes(target, types, G_N_ELEMENTS(types));
    g_signal_connect(target, "drop", G_CALLBACK(dropped), ui);
    gtk_widget_add_controller(drop, GTK_EVENT_CONTROLLER(target));
    GtkWidget *installed = gtk_button_new_with_label("从已安装应用创建桌面快捷方式…");
    gtk_widget_set_halign(installed, GTK_ALIGN_FILL);
    g_signal_connect(installed, "clicked", G_CALLBACK(open_shortcut_picker), ui);
    gtk_box_append(GTK_BOX(form), installed);
    gtk_box_append(GTK_BOX(form), text("为已安装的图形应用一键创建桌面快捷方式。", "dim-label"));
    ui->source = field(GTK_BOX(form), "文件路径", "选择程序、脚本、文档或快捷方式");
    gtk_editable_set_editable(GTK_EDITABLE(ui->source), FALSE);
    ui->name = field(GTK_BOX(form), "显示名称", "给启动项起个名字");
    ui->type_hint = GTK_LABEL(text("登录桌面后启动，不会在添加时运行文件。", "dim-label"));
    gtk_box_append(GTK_BOX(form), GTK_WIDGET(ui->type_hint));
    GtkWidget *advanced = gtk_expander_new("高级设置");
    GtkWidget *advanced_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_widget_set_margin_top(advanced_box, 10);
    ui->cwd = field(GTK_BOX(advanced_box), "工作目录", "默认使用文件所在文件夹");
    ui->args = field(GTK_BOX(advanced_box), "启动参数", "例如：--profile '个人配置'");
    gtk_box_append(GTK_BOX(advanced_box), text("空格分隔参数；含空格的参数用引号包裹。", "caption"));
    ui->terminal = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("在终端中运行"));
    gtk_box_append(GTK_BOX(advanced_box), GTK_WIDGET(ui->terminal));
    ui->watchdog = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("启用看门狗：程序退出后自动重启"));
    gtk_widget_set_sensitive(GTK_WIDGET(ui->watchdog), FALSE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(ui->watchdog), "支持普通可执行程序、可识别进程的应用快捷方式，以及可执行的 .run 启动器");
    gtk_box_append(GTK_BOX(advanced_box), GTK_WIDGET(ui->watchdog));
    gtk_expander_set_child(GTK_EXPANDER(advanced), advanced_box);
    gtk_box_append(GTK_BOX(form), advanced);
    ui->save = GTK_BUTTON(gtk_button_new_with_label("添加自启动"));
    gtk_widget_add_css_class(GTK_WIDGET(ui->save), "suggested-action");
    gtk_widget_add_css_class(GTK_WIDGET(ui->save), "pill");
    g_signal_connect(ui->save, "clicked", G_CALLBACK(save_item), ui);
    gtk_box_append(GTK_BOX(form), GTK_WIDGET(ui->save));
    GtkWidget *reset = gtk_button_new_with_label("清空 / 取消编辑");
    gtk_widget_add_css_class(reset, "flat");
    g_signal_connect(reset, "clicked", G_CALLBACK(reset_clicked), ui);
    gtk_box_append(GTK_BOX(form), reset);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_hexpand(right, TRUE);
    gtk_widget_set_size_request(right, 540, -1);
    gtk_box_append(GTK_BOX(layout), right);
    gtk_box_append(GTK_BOX(right), text("我的登录自启动", "title-2"));
    ui->count = GTK_LABEL(text("", "dim-label"));
    gtk_box_append(GTK_BOX(right), GTK_WIDGET(ui->count));
    ui->helper_status = GTK_LABEL(text("", "dim-label"));
    gtk_box_append(GTK_BOX(right), GTK_WIDGET(ui->helper_status));
    ui->list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(ui->list, GTK_SELECTION_NONE);
    gtk_widget_add_css_class(GTK_WIDGET(ui->list), "boxed-list");
    gtk_box_append(GTK_BOX(right), GTK_WIDGET(ui->list));
    ui->empty = adw_status_page_new();
    adw_status_page_set_icon_name(ADW_STATUS_PAGE(ui->empty), "io.github.local.LoginStartup");
    adw_status_page_set_title(ADW_STATUS_PAGE(ui->empty), "从一个常用文件开始");
    adw_status_page_set_description(ADW_STATUS_PAGE(ui->empty), "选择左侧的文件并添加。\n下次登录桌面时，它就会自动启动。");
    gtk_widget_set_vexpand(ui->empty, TRUE);
    gtk_widget_add_css_class(ui->empty, "card");
    gtk_box_append(GTK_BOX(right), ui->empty);
    GtkWidget *note = text("仅管理本软件创建的启动项 · 添加不会运行文件\n启用看门狗后，助手会在登录时启动后台监控 · 删除配置不会删除原文件", "dim-label");
    gtk_widget_set_margin_top(note, 12);
    gtk_box_append(GTK_BOX(right), note);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        ":root { --accent-bg-color: #137e79; --accent-fg-color: white; }"
        "@define-color accent_bg_color #137e79; @define-color accent_color #137e79;"
        ".drop-zone { padding: 24px 18px; border: 2px dashed alpha(@accent_bg_color, 0.45); border-radius: 18px; background: mix(@window_bg_color, #9ac8d3, 0.13); }"
        ".drop-zone:drop(active) { background: alpha(@accent_bg_color, 0.16); border-color: @accent_bg_color; }"
        "entry { min-height: 34px; } .boxed-list row { padding: 8px 4px; } .caption-heading { margin-top: 5px; }");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(GTK_WIDGET(ui->window)), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    refresh(ui);
    g_timeout_add_seconds(2, update_helper_status, ui);
    gtk_window_present(ui->window);
}
int ls_ui(int argc, char **argv) {
    Ui ui = {0};
    ui.app = adw_application_new("io.github.local.LoginStartup", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(ui.app, "activate", G_CALLBACK(activate), &ui);
    int result = g_application_run(G_APPLICATION(ui.app), argc, argv);
    g_clear_pointer(&ui.items, g_ptr_array_unref);
    g_clear_pointer(&ui.installed_apps, g_ptr_array_unref);
    g_free(ui.editing_id); g_object_unref(ui.app);
    return result;
}
