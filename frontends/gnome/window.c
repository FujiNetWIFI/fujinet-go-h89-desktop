/*
 * window.c -- the main window: the H19's screen, the header-bar menu, the
 * keyboard, the drives, the FujiNet status, the first-run ROM import.
 *
 * Keys go to the H19 as characters: GDK's keyval and the character it types
 * become an H19 key through the session's one tested table
 * (h89session_key_from_event), so the H19 sees the layout the user has.
 * CTRL with a letter is a control code -- CP/M and HDOS live on them -- so
 * the app's own shortcuts are CTRL+SHIFT ones (and F9, F11, F12, which the
 * H19 has no use for).
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "window.h"

#include "debugger/dbg_window.h"
#include "display.h"
#include "fujilog.h"
#include "keys_panel.h"
#include "prefs.h"

#include <string.h>

#define MAX_DRIVES 32

struct _H89Window {
    AdwApplicationWindow parent_instance;

    h89session *session;
    GtkWidget *display;
    GtkWidget *toast_overlay;
    GtkWidget *status;          /* the FujiNet link, the busy drive */
    GtkWidget *status_dot;
    GtkWidget *disk_dot;
    GtkWidget *menu_button;
    GMenu *drives_menu;         /* one submenu per drive, rebuilt as they change */
    char drives_sig[4096];      /* what it last showed */
    char wp_actions[MAX_DRIVES][32];
    int wp_action_count;
    guint status_id;
    guint fast_id;
    guint flash_id;
    gboolean fullscreen;
    gboolean rom_setup_open;
};

G_DEFINE_FINAL_TYPE(H89Window, h89_window, ADW_TYPE_APPLICATION_WINDOW)

static void rebuild_drives_menu(H89Window *self, gboolean force);
static void update_status(H89Window *self);
static GMenu *build_menu(H89Window *self);

void h89_window_toast(H89Window *self, const char *text)
{
    adw_toast_overlay_add_toast(ADW_TOAST_OVERLAY(self->toast_overlay), adw_toast_new(text));
}

void h89_install_accent_css(void)
{
    static gboolean done;
    GtkCssProvider *css;
    char buf[768];
    if (done)
        return;
    done = TRUE;
    /* black on the nameplate's gold: the gold is light */
    g_snprintf(buf, sizeof buf,
        ".h89-accent { background: #%06x; color: #000000; }\n"
        ".h89-accent:hover { background: #%06x; }\n"
        ".h89-accent-text { color: #%06x; font-weight: bold; }\n"
        ".h89-dot { border-radius: 6px; min-width: 12px; min-height: 12px; }\n"
        ".h89-dot-on { background: #%06x; }\n"
        ".h89-dot-off { background: #808080; }\n"
        ".h89-flash { filter: invert(1); }\n",
        H89SESSION_ACCENT_RGB, H89SESSION_ACCENT_RGB, H89SESSION_ACCENT_RGB,
        H89SESSION_ACCENT_RGB);
    css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css, buf);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

static void show_message(GtkWidget *over, const char *heading, const char *body)
{
    AdwDialog *dlg = adw_alert_dialog_new(heading, body);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "ok", "OK");
    adw_dialog_present(dlg, over);
}

static const char *base_name(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

/* ---- the keyboard ------------------------------------------------------------ */

gboolean h89_window_key(H89Window *self, guint keyval, GdkModifierType state)
{
    gboolean ctrl = (state & GDK_CONTROL_MASK) != 0;
    gboolean shift = (state & GDK_SHIFT_MASK) != 0;
    int key;

    /* the window's own keys: the H19 has no F9, F11 or F12 */
    if (keyval == GDK_KEY_F9) {
        h89_keys_panel_toggle(GTK_WINDOW(self), self->session);
        return TRUE;
    }
    if (keyval == GDK_KEY_F11) {
        gtk_widget_activate_action(GTK_WIDGET(self), "win.fullscreen", NULL);
        return TRUE;
    }
    if (keyval == GDK_KEY_F12) {
        h89_debugger_toggle(GTK_WINDOW(self), self->session);
        return TRUE;
    }
    /* CTRL+SHIFT and ALT are the app's shortcuts, and CTRL+, is
     * Preferences (a comma has no control code) */
    if ((state & (GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK)) || (ctrl && shift) ||
        (ctrl && keyval == GDK_KEY_comma))
        return FALSE;
    if (keyval == GDK_KEY_ISO_Left_Tab)
        keyval = GDK_KEY_Tab;

    key = h89session_key_from_event(keyval, gdk_keyval_to_unicode(keyval), ctrl, shift);
    if (key < 0)
        return FALSE;
    h89session_key(self->session, key, shift);
    return TRUE;
}

/* An open dialog (Preferences, About, an alert) or menu has the keyboard:
 * Escape closes it and the arrows and Return move through it, instead of
 * going to the H19. The key capture sits on the window, ahead of them. */
static gboolean ui_has_keyboard(H89Window *self)
{
    GtkWidget *focus = gtk_root_get_focus(GTK_ROOT(self));
    if (adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(self)))
        return TRUE;
    return focus && gtk_widget_get_ancestor(focus, GTK_TYPE_POPOVER) != NULL;
}

static gboolean on_key_pressed(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                               GdkModifierType state, gpointer user_data)
{
    H89Window *self = user_data;
    (void)ctrl; (void)keycode;
    if (ui_has_keyboard(self))
        return FALSE;
    return h89_window_key(self, keyval, state);
}

/* ---- starting, and the ROMs it needs ------------------------------------------- */

static void show_rom_setup(H89Window *self);

int h89_show_rom_import(GtkWidget *over, h89session *session, const char *path)
{
    char msg[1024];
    int n = h89session_import_roms(session, path, msg, sizeof msg);
    show_message(over, n > 0 ? "ROMs Imported" : "Nothing Imported",
                 msg[0] ? msg : h89session_last_error(session));
    return n;
}

static void started(H89Window *self)
{
    self->drives_sig[0] = '\0';
    rebuild_drives_menu(self, TRUE);
    update_status(self);
}

void h89_window_start(H89Window *self)
{
    h89session_start_opts opts;
    char what[512];

    if (h89session_is_running(self->session))
        return;
    h89session_default_opts(self->session, &opts);
    if (h89session_start(self->session, &opts) == 0) {
        started(self);
        return;
    }
    if (h89session_missing_roms(self->session, what, sizeof what) > 0) {
        show_rom_setup(self);
        return;
    }
    show_message(GTK_WIDGET(self), "Cannot Start the H89", h89session_last_error(self->session));
    update_status(self);
}

/* After an import: start when nothing is missing any more, else say what
 * still is. */
static void after_rom_import(H89Window *self, int imported, GString *log)
{
    char what[512];
    int missing = h89session_missing_roms(self->session, what, sizeof what);

    if (!h89session_is_running(self->session) && missing == 0) {
        h89_window_start(self);
        if (h89session_is_running(self->session))
            h89_window_toast(self, "ROMs imported \xe2\x80\x94 the H89 is on");
        return;
    }
    if (!h89session_is_running(self->session)) {
        show_rom_setup(self);
        return;
    }
    show_message(GTK_WIDGET(self), imported > 0 ? "ROMs Imported" : "Nothing Imported", log->str);
}

static void on_roms_chosen(GObject *src, GAsyncResult *res, gpointer user_data)
{
    H89Window *self = user_data;
    g_autoptr(GListModel) files =
        gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(src), res, NULL);
    GString *log = g_string_new(NULL);
    int imported = 0;

    if (files) {
        for (guint i = 0; i < g_list_model_get_n_items(files); i++) {
            g_autoptr(GFile) f = g_list_model_get_item(files, i);
            g_autofree char *path = g_file_get_path(f);
            char msg[1024];
            int n;
            if (!path)
                continue;
            n = h89session_import_roms(self->session, path, msg, sizeof msg);
            if (n > 0)
                imported += n;
            g_string_append_printf(log, "%s%s", log->len ? "\n\n" : "", msg);
        }
        after_rom_import(self, imported, log);
    } else if (!h89session_is_running(self->session)) {
        update_status(self);
    }
    g_string_free(log, TRUE);
}

static void choose_rom_files(H89Window *self)
{
    GtkFileDialog *dlg = gtk_file_dialog_new();
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *roms = gtk_file_filter_new();
    GtkFileFilter *all = gtk_file_filter_new();

    gtk_file_filter_set_name(roms, "MAME ROM sets and ROM images (*.zip, *.bin, *.rom, *.u5xx)");
    gtk_file_filter_add_suffix(roms, "zip");
    gtk_file_filter_add_suffix(roms, "bin");
    gtk_file_filter_add_suffix(roms, "rom");
    gtk_file_filter_add_pattern(roms, "*.u[0-9][0-9][0-9]");
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    g_list_store_append(filters, roms);
    g_list_store_append(filters, all);
    gtk_file_dialog_set_title(dlg, "Import System ROMs");
    gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));
    gtk_file_dialog_open_multiple(dlg, GTK_WINDOW(self), NULL, on_roms_chosen, self);
    g_object_unref(roms);
    g_object_unref(all);
    g_object_unref(filters);
    g_object_unref(dlg);
}

static void on_rom_setup_response(AdwAlertDialog *dlg, const char *response, gpointer user_data)
{
    H89Window *self = user_data;
    (void)dlg;
    self->rom_setup_open = FALSE;
    if (g_strcmp0(response, "import") == 0)
        choose_rom_files(self);
    else
        update_status(self);
}

static void show_rom_setup(H89Window *self)
{
    char what[512], body[1200];
    AdwDialog *dlg;

    if (self->rom_setup_open)
        return;
    /* asked for at start; a ROM file named on the command line or dropped
     * may have been imported since */
    if (h89session_missing_roms(self->session, what, sizeof what) == 0)
        return;
    g_snprintf(body, sizeof body,
        "FujiNet Go H89 does not come with the Heath ROMs. Import them from "
        "MAME's h89.zip and heath_tlb.zip, or from ROM image files: they are "
        "recognised by their contents and kept in\n%s\n\nStill needed: %s.",
        h89session_roms_path(self->session), what);
    dlg = adw_alert_dialog_new("Import the H89's ROMs", body);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "later", "Not Now");
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "import", "Import ROMs\xe2\x80\xa6");
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dlg), "import",
                                             ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dlg), "import");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dlg), "later");
    g_signal_connect(dlg, "response", G_CALLBACK(on_rom_setup_response), self);
    self->rom_setup_open = TRUE;
    adw_dialog_present(dlg, GTK_WIDGET(self));
}

void h89_window_restart(H89Window *self, gboolean host_options_changed)
{
    if (!h89session_is_running(self->session)) {
        h89_window_start(self);
        return;
    }
    if (host_options_changed) {
        h89session_stop(self->session);
        h89_window_start(self);
        if (h89session_is_running(self->session))
            h89_window_toast(self, "Restarted with the new options");
        return;
    }
    if (h89session_power_cycle(self->session) != 0) {
        show_message(GTK_WIDGET(self), "Cannot Power Cycle", h89session_last_error(self->session));
        update_status(self);
        return;
    }
    started(self);
    h89_window_toast(self, "Power cycled with the new configuration");
}

/* ---- status ------------------------------------------------------------------ */

static void set_dot(GtkWidget *dot, gboolean on)
{
    gtk_widget_remove_css_class(dot, on ? "h89-dot-off" : "h89-dot-on");
    gtk_widget_add_css_class(dot, on ? "h89-dot-on" : "h89-dot-off");
}

static void update_status(H89Window *self)
{
    char text[512];
    int link;

    if (!h89session_is_running(self->session)) {
        char what[512];
        g_snprintf(text, sizeof text, "%s",
                   h89session_missing_roms(self->session, what, sizeof what) > 0
                       ? "Off \xe2\x80\x94 ROMs needed (Machine \xe2\x96\xb8 Import System ROMs)"
                       : "Off");
        gtk_label_set_text(GTK_LABEL(self->status), text);
        set_dot(self->status_dot, FALSE);
        return;
    }
    link = h89session_card_link(self->session);
    g_snprintf(text, sizeof text, "%s%s",
               link < 0 ? "No FujiNet card" : h89session_card_link_text(link),
               h89session_warp(self->session) ? "  \xc2\xb7  WARP" : "");
    gtk_label_set_text(GTK_LABEL(self->status), text);
    set_dot(self->status_dot, link == 2);
}

static gboolean status_tick(gpointer user_data)
{
    H89Window *self = user_data;
    update_status(self);
    rebuild_drives_menu(self, FALSE);
    return G_SOURCE_CONTINUE;
}

static gboolean unflash(gpointer user_data)
{
    H89Window *self = user_data;
    gtk_widget_remove_css_class(self->display, "h89-flash");
    self->flash_id = 0;
    return G_SOURCE_REMOVE;
}

/* The drive light, and the visual bell: four times a second. */
static gboolean fast_tick(gpointer user_data)
{
    H89Window *self = user_data;
    gboolean busy = FALSE;
    int n = h89session_drive_count(self->session);

    for (int i = 0; i < n; i++) {
        h89_drive_info d;
        if (h89session_drive_info_at(self->session, i, &d) == 0 && d.active) {
            gtk_widget_set_tooltip_text(self->disk_dot, d.label);
            busy = TRUE;
            break;
        }
    }
    set_dot(self->disk_dot, busy);
    if (!busy)
        gtk_widget_set_tooltip_text(self->disk_dot, "Drive activity");

    if (h89session_take_visual_bells(self->session) > 0 &&
        h89session_get_int(self->session, "sound.visual_bell", 0) && !self->flash_id) {
        gtk_widget_add_css_class(self->display, "h89-flash");
        self->flash_id = g_timeout_add(120, unflash, self);
    }
    return G_SOURCE_CONTINUE;
}

/* ---- drives ---------------------------------------------------------------------- */

static const char *const H17_EXTS[] = { "h8d", "h17", "h17disk", "h17raw", "rawdisk",
                                        "tmpdisk", NULL };
static const char *const SOFT_EXTS[] = { "imd", "td0", "mmsdisk", "logdisk", "rawfloppy",
                                         NULL };

static GtkFileDialog *file_dialog(const char *title, const char *filter_name,
                                  const char *const *a, const char *const *b,
                                  const char *folder)
{
    GtkFileDialog *dlg = gtk_file_dialog_new();
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *images = gtk_file_filter_new();
    GtkFileFilter *all = gtk_file_filter_new();

    gtk_file_filter_set_name(images, filter_name);
    for (int i = 0; a && a[i]; i++)
        gtk_file_filter_add_suffix(images, a[i]);
    for (int i = 0; b && b[i]; i++)
        gtk_file_filter_add_suffix(images, b[i]);
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    g_list_store_append(filters, images);
    g_list_store_append(filters, all);
    gtk_file_dialog_set_title(dlg, title);
    gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));
    if (folder && *folder) {
        g_autoptr(GFile) dir = g_file_new_for_path(folder);
        gtk_file_dialog_set_initial_folder(dlg, dir);
    }
    g_object_unref(images);
    g_object_unref(all);
    g_object_unref(filters);
    return dlg;
}

/* Where to look for a disk: the folder the last one came from. */
static char *disks_folder(H89Window *self)
{
    const char *last = h89session_get_str(self->session, "drives.last_dir", "");
    if (last && *last && g_file_test(last, G_FILE_TEST_IS_DIR))
        return g_strdup(last);
    return g_strdup(h89session_disks_path(self->session));
}

static void remember_folder(H89Window *self, const char *path)
{
    g_autofree char *dir = g_path_get_dirname(path);
    h89session_set_str(self->session, "drives.last_dir", dir);
}

static void insert_into(H89Window *self, int id, const char *path)
{
    char err[512], msg[700];
    h89_drive_info d;
    if (h89session_drive_insert(self->session, id, path, 0, err, sizeof err) != 0) {
        show_message(GTK_WIDGET(self), "Cannot Insert the Disk", err);
        return;
    }
    remember_folder(self, path);
    if (h89session_drive_info(self->session, id, &d) == 0) {
        g_snprintf(msg, sizeof msg, "%s is in %s", base_name(path), d.label);
        h89_window_toast(self, msg);
    }
    rebuild_drives_menu(self, FALSE);
}

typedef struct {
    H89Window *self;
    int id;
} InsertCtx;

static void on_insert_chosen(GObject *src, GAsyncResult *res, gpointer user_data)
{
    InsertCtx *c = user_data;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    g_autofree char *path = file ? g_file_get_path(file) : NULL;
    if (path)
        insert_into(c->self, c->id, path);
    g_free(c);
}

static void action_drive_insert(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    int id = g_variant_get_int32(p);
    gboolean h17 = H89_DRIVE_CTRL(id) == H89_CTRL_H17;
    g_autofree char *folder = disks_folder(self);
    h89_drive_info d;
    char title[160];
    InsertCtx *c;
    GtkFileDialog *dlg;
    (void)a;

    if (h89session_drive_info(self->session, id, &d) != 0)
        return;
    g_snprintf(title, sizeof title, "Insert a Disk in %s", d.label);
    dlg = file_dialog(title,
                      h17 ? "H17 disk images (*.h8d, *.h17, *.h17raw, ...)"
                          : "Soft-sectored disk images (*.imd, *.td0, *.mmsdisk, ...)",
                      h17 ? H17_EXTS : SOFT_EXTS, NULL, folder);
    c = g_new0(InsertCtx, 1);
    c->self = self;
    c->id = id;
    gtk_file_dialog_open(dlg, GTK_WINDOW(self), NULL, on_insert_chosen, c);
    g_object_unref(dlg);
}

static void action_drive_eject(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    int id = g_variant_get_int32(p);
    char err[512];
    h89_drive_info d;
    (void)a;
    if (h89session_drive_info(self->session, id, &d) != 0 || !d.loaded)
        return;
    if (h89session_drive_eject(self->session, id, err, sizeof err) != 0) {
        show_message(GTK_WIDGET(self), "Cannot Eject the Disk", err);
        return;
    }
    rebuild_drives_menu(self, FALSE);
}

static void on_wp_change(GSimpleAction *a, GVariant *value, gpointer user_data)
{
    H89Window *self = user_data;
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(a), "h89-drive"));
    gboolean on = g_variant_get_boolean(value);
    h89_drive_info d;
    if (h89session_drive_info(self->session, id, &d) != 0 || !d.loaded ||
        (d.read_only_format && !on))
        return;
    h89session_drive_set_write_protect(self->session, id, on);
    g_simple_action_set_state(a, g_variant_new_boolean(on));
}

/* GMenu labels take _ as a mnemonic: a file name keeps its own. */
static char *menu_escape(const char *s)
{
    GString *g = g_string_new(NULL);
    for (; *s; s++) {
        if (*s == '_')
            g_string_append_c(g, '_');
        g_string_append_c(g, *s);
    }
    return g_string_free(g, FALSE);
}

static void rebuild_drives_menu(H89Window *self, gboolean force)
{
    char sig[4096] = "";
    int n = h89session_drive_count(self->session);
    GMenu *drives;

    for (int i = 0; i < n; i++) {
        h89_drive_info d;
        if (h89session_drive_info_at(self->session, i, &d) != 0)
            continue;
        g_strlcat(sig, d.label, sizeof sig);
        g_strlcat(sig, d.path, sizeof sig);
        g_strlcat(sig, d.write_protected ? "+" : "-", sizeof sig);
    }
    if (!force && strcmp(sig, self->drives_sig) == 0)
        return;
    g_strlcpy(self->drives_sig, sig, sizeof self->drives_sig);

    for (int i = 0; i < self->wp_action_count; i++)
        g_action_map_remove_action(G_ACTION_MAP(self), self->wp_actions[i]);
    self->wp_action_count = 0;

    /* a fresh menu each time: GtkPopoverMenu names its submenu pages by
     * label, and changing the model it shows in place re-adds pages whose
     * names are still taken */
    g_clear_object(&self->drives_menu);
    self->drives_menu = g_menu_new();
    drives = g_menu_new();
    for (int i = 0; i < n && self->wp_action_count < MAX_DRIVES; i++) {
        h89_drive_info d;
        GMenu *sub;
        GMenuItem *item;
        GSimpleAction *wp;
        g_autofree char *label = NULL;
        g_autofree char *file = NULL;
        char *name;

        if (h89session_drive_info_at(self->session, i, &d) != 0 || !d.removable)
            continue;
        file = menu_escape(d.loaded ? base_name(d.path) : "empty");
        label = g_strdup_printf("%s: %s", d.label, file);
        sub = g_menu_new();

        item = g_menu_item_new("_Insert\xe2\x80\xa6", NULL);
        g_menu_item_set_action_and_target_value(item, "win.drive-insert",
                                                g_variant_new_int32(d.id));
        g_menu_append_item(sub, item);
        g_object_unref(item);
        if (d.loaded) {
            item = g_menu_item_new("_Eject", NULL);
            g_menu_item_set_action_and_target_value(item, "win.drive-eject",
                                                    g_variant_new_int32(d.id));
            g_menu_append_item(sub, item);
            g_object_unref(item);

            name = self->wp_actions[self->wp_action_count++];
            g_snprintf(name, sizeof self->wp_actions[0], "drive-wp-%d", d.id);
            wp = g_simple_action_new_stateful(name, NULL,
                                              g_variant_new_boolean(d.write_protected));
            g_object_set_data(G_OBJECT(wp), "h89-drive", GINT_TO_POINTER(d.id));
            g_simple_action_set_enabled(wp, !d.read_only_format);
            g_signal_connect(wp, "change-state", G_CALLBACK(on_wp_change), self);
            g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(wp));
            g_object_unref(wp);
            {
                char action[48];
                g_snprintf(action, sizeof action, "win.%s", name);
                g_menu_append(sub, "_Write Protect", action);
            }
        }
        g_menu_append_submenu(drives, label, G_MENU_MODEL(sub));
        g_object_unref(sub);
    }
    if (g_menu_model_get_n_items(G_MENU_MODEL(drives)) == 0)
        g_menu_append(drives, "No drives (fit a controller in Preferences)", "win.prefs");
    g_menu_append_section(self->drives_menu, NULL, G_MENU_MODEL(drives));
    g_object_unref(drives);

    {
        GMenu *more = g_menu_new();
        g_menu_append(more, "Insert _Disk\xe2\x80\xa6", "win.insert-disk");
        g_menu_append(more, "_New Blank Disk\xe2\x80\xa6", "win.new-disk");
        g_menu_append(more, "Open Disks _Folder", "win.disks-folder");
        g_menu_append_section(self->drives_menu, NULL, G_MENU_MODEL(more));
        g_object_unref(more);
    }
    if (self->menu_button) {
        GMenu *menu = build_menu(self);
        gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(self->menu_button), G_MENU_MODEL(menu));
        g_object_unref(menu);
    }
}

/* "Which drive?" when every drive that takes a disk is full. */
typedef struct {
    H89Window *self;
    char *path;
} ChooseCtx;

static void on_choose_drive(AdwAlertDialog *dlg, const char *response, gpointer user_data)
{
    ChooseCtx *c = user_data;
    (void)dlg;
    if (response && response[0] == 'd')
        insert_into(c->self, atoi(response + 1), c->path);
    g_free(c->path);
    g_free(c);
}

static void ask_which_drive(H89Window *self, const char *path, const int *ids, int n)
{
    char heading[300];
    AdwDialog *dlg;
    ChooseCtx *c = g_new0(ChooseCtx, 1);

    g_snprintf(heading, sizeof heading, "Which Drive Should %s Go In?", base_name(path));
    dlg = adw_alert_dialog_new(heading, "Every drive that takes it has a disk in it: "
                                        "the one chosen is ejected first.");
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "cancel", "Cancel");
    for (int i = 0; i < n; i++) {
        h89_drive_info d;
        char id[16];
        if (h89session_drive_info(self->session, ids[i], &d) != 0)
            continue;
        g_snprintf(id, sizeof id, "d%d", ids[i]);
        adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), id, d.label);
    }
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dlg), "cancel");
    c->self = self;
    c->path = g_strdup(path);
    g_signal_connect(dlg, "response", G_CALLBACK(on_choose_drive), c);
    adw_dialog_present(dlg, GTK_WIDGET(self));
}

void h89_window_load_media(H89Window *self, const char *path)
{
    char msg[1024];
    int ids[16], nids = 0;
    int kind = h89session_media_kind(path);
    int rc;

    if (kind == H89_MEDIA_ROMS) {
        GString *log = g_string_new(NULL);
        int n = h89session_import_roms(self->session, path, msg, sizeof msg);
        g_string_append(log, msg);
        after_rom_import(self, n, log);
        g_string_free(log, TRUE);
        return;
    }
    if (!h89session_is_running(self->session) && kind != H89_MEDIA_UNKNOWN) {
        h89_window_toast(self, "The H89 is off: import its ROMs first");
        return;
    }
    rc = h89session_import_media(self->session, path, msg, sizeof msg, ids, 16, &nids);
    if (rc < 0) {
        show_message(GTK_WIDGET(self), "Cannot Use the File", msg);
        return;
    }
    if (rc == 1) {
        ask_which_drive(self, path, ids, nids);
        return;
    }
    if (kind != H89_MEDIA_UNKNOWN)
        remember_folder(self, path);
    h89_window_toast(self, msg);
    rebuild_drives_menu(self, FALSE);
}

static void on_any_disk_chosen(GObject *src, GAsyncResult *res, gpointer user_data)
{
    H89Window *self = user_data;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    g_autofree char *path = file ? g_file_get_path(file) : NULL;
    if (path)
        h89_window_load_media(self, path);
}

static void action_insert_disk(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    g_autofree char *folder = disks_folder(self);
    GtkFileDialog *dlg = file_dialog("Insert a Disk", "Disk images", H17_EXTS, SOFT_EXTS, folder);
    (void)a; (void)p;
    gtk_file_dialog_open(dlg, GTK_WINDOW(self), NULL, on_any_disk_chosen, self);
    g_object_unref(dlg);
}

/* New Blank Disk: the kind, then where to save it; then it goes in a drive. */
typedef struct {
    H89Window *self;
    GtkWidget *kind, *sides, *tracks;
    int k, s, t;
} BlankCtx;

static const char *const BLANK_KINDS[] = {
    "H17: sector image (.h8d) \xe2\x80\x94 format it with HDOS INIT",
    "H17: H17Disk (.h17)",
    "H17: raw tracks (.h17raw)",
    "Soft-sectored: ImageDisk (.imd) \xe2\x80\x94 format it in the H89",
    NULL,
};
static const char *const BLANK_EXTS[] = { "h8d", "h17", "h17raw", "imd" };

static void on_blank_saved(GObject *src, GAsyncResult *res, gpointer user_data)
{
    BlankCtx *c = user_data;
    g_autoptr(GFile) file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, NULL);
    g_autofree char *path = file ? g_file_get_path(file) : NULL;
    char err[512];

    if (path) {
        if (h89session_new_blank_disk(c->self->session, path, c->k, c->s, c->t, err,
                                      sizeof err) != 0)
            show_message(GTK_WIDGET(c->self), "Cannot Create the Disk", err);
        else
            h89_window_load_media(c->self, path);
    }
    g_free(c);
}

static void on_blank_kind(GObject *dd, GParamSpec *pspec, gpointer user_data)
{
    BlankCtx *c = user_data;
    gboolean imd = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd)) == H89_BLANK_IMD;
    (void)pspec;
    gtk_widget_set_sensitive(c->sides, !imd);
    gtk_widget_set_sensitive(c->tracks, !imd);
}

static void on_blank_response(AdwAlertDialog *dlg, const char *response, gpointer user_data)
{
    BlankCtx *c = user_data;
    GtkFileDialog *save;
    char name[32];
    (void)dlg;
    if (g_strcmp0(response, "create") != 0) {
        g_free(c);
        return;
    }
    c->k = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(c->kind));
    c->s = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->sides)) == 0 ? 1 : 2;
    c->t = gtk_drop_down_get_selected(GTK_DROP_DOWN(c->tracks)) == 0 ? 40 : 80;
    if (c->k < 0 || c->k >= H89_BLANK_KIND_COUNT)
        c->k = 0;
    h89session_set_int(c->self->session, "blank.kind", c->k);
    h89session_set_int(c->self->session, "blank.sides", c->s);
    h89session_set_int(c->self->session, "blank.tracks", c->t);
    save = gtk_file_dialog_new();
    gtk_file_dialog_set_title(save, "Save the New Disk");
    g_snprintf(name, sizeof name, "blank.%s", BLANK_EXTS[c->k]);
    gtk_file_dialog_set_initial_name(save, name);
    {
        g_autoptr(GFile) dir = g_file_new_for_path(h89session_disks_path(c->self->session));
        gtk_file_dialog_set_initial_folder(save, dir);
    }
    gtk_file_dialog_save(save, GTK_WINDOW(c->self), NULL, on_blank_saved, c);
    g_object_unref(save);
}

static GtkWidget *labelled(const char *label, GtkWidget *w)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *l = gtk_label_new(label);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_widget_set_hexpand(l, TRUE);
    gtk_box_append(GTK_BOX(box), l);
    gtk_box_append(GTK_BOX(box), w);
    return box;
}

static void action_new_disk(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    static const char *const sides[] = { "1", "2", NULL };
    static const char *const tracks[] = { "40", "80", NULL };
    BlankCtx *c = g_new0(BlankCtx, 1);
    AdwDialog *dlg;
    GtkWidget *box;
    (void)a; (void)p;

    c->self = self;
    c->kind = gtk_drop_down_new_from_strings(BLANK_KINDS);
    c->sides = gtk_drop_down_new_from_strings(sides);
    c->tracks = gtk_drop_down_new_from_strings(tracks);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c->kind),
                               (guint)h89session_get_int(self->session, "blank.kind", 0));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c->sides),
                               h89session_get_int(self->session, "blank.sides", 1) == 2);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c->tracks),
                               h89session_get_int(self->session, "blank.tracks", 40) == 80);
    g_signal_connect(c->kind, "notify::selected", G_CALLBACK(on_blank_kind), c);
    on_blank_kind(G_OBJECT(c->kind), NULL, c);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_append(GTK_BOX(box), c->kind);
    gtk_box_append(GTK_BOX(box), labelled("Sides", c->sides));
    gtk_box_append(GTK_BOX(box), labelled("Tracks", c->tracks));

    dlg = adw_alert_dialog_new("New Blank Disk",
        "An H17 disk is 40 tracks single sided for the H-17-1 drive, 80 "
        "tracks double sided for the H-17-4. An ImageDisk takes whatever "
        "the H89 formats on it.");
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dlg), box);
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "cancel", "Cancel");
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dlg), "create", "Create\xe2\x80\xa6");
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dlg), "create",
                                             ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dlg), "create");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dlg), "cancel");
    g_signal_connect(dlg, "response", G_CALLBACK(on_blank_response), c);
    adw_dialog_present(dlg, GTK_WIDGET(self));
}

static void open_folder(H89Window *self, const char *path)
{
    g_autoptr(GFile) dir = g_file_new_for_path(path);
    GtkFileLauncher *l = gtk_file_launcher_new(dir);
    gtk_file_launcher_launch(l, GTK_WINDOW(self), NULL, NULL, NULL);
    g_object_unref(l);
}

static void action_disks_folder(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    g_mkdir_with_parents(h89session_disks_path(self->session), 0755);
    open_folder(self, h89session_disks_path(self->session));
}

static gboolean on_drop(GtkDropTarget *t, const GValue *value, double x, double y,
                        gpointer user_data)
{
    H89Window *self = user_data;
    g_autofree char *path = NULL;
    (void)t; (void)x; (void)y;
    if (!G_VALUE_HOLDS(value, G_TYPE_FILE))
        return FALSE;
    path = g_file_get_path(G_FILE(g_value_get_object(value)));
    if (!path)
        return FALSE;
    h89_window_load_media(self, path);
    return TRUE;
}

/* ---- the machine ------------------------------------------------------------------ */

static void action_reset(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    h89session_reset(self->session);
}

static void action_power_cycle(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    h89_window_restart(self, FALSE);
}

static void action_warp(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    gboolean on = !g_variant_get_boolean(g_action_get_state(G_ACTION(a)));
    (void)p;
    g_simple_action_set_state(a, g_variant_new_boolean(on));
    h89session_set_warp(self->session, on);
    update_status(self);
}

static void action_import_roms(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    (void)a; (void)p;
    choose_rom_files(user_data);
}

static void action_prefs(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    h89_prefs_show(self, self->session);
}

static void action_quit(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    (void)a; (void)p;
    gtk_window_close(GTK_WINDOW(user_data));
}

/* ---- text in and out ---------------------------------------------------------------- */

static void action_copy_screen(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    static char buf[8192];
    (void)a; (void)p;
    if (h89session_screen_text(self->session, buf, sizeof buf, 1) == 0)
        return;
    gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(self)), buf);
    h89_window_toast(self, "The screen's text is on the clipboard");
}

static void on_paste_text(GObject *src, GAsyncResult *res, gpointer user_data)
{
    H89Window *self = user_data;
    g_autofree char *text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(src), res, NULL);
    if (text && *text)
        h89session_paste(self->session, text);
}

static void action_paste(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    gdk_clipboard_read_text_async(gtk_widget_get_clipboard(GTK_WIDGET(self)), NULL,
                                  on_paste_text, self);
}

/* ---- the view ------------------------------------------------------------------------ */

static void action_keys(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    h89_keys_panel_toggle(GTK_WINDOW(self), self->session);
}

static void action_debugger(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    h89_debugger_toggle(GTK_WINDOW(self), self->session);
}

static void action_fullscreen(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    self->fullscreen = !self->fullscreen;
    if (self->fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(self));
    else
        gtk_window_unfullscreen(GTK_WINDOW(self));
}

static void action_aspect(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    gboolean tv = !g_variant_get_boolean(g_action_get_state(G_ACTION(a)));
    (void)p;
    h89session_set_int(self->session, "view.aspect", tv ? 0 : 1);
    h89_window_apply_aspect(self, tv ? 0 : 1);
}

static void action_smooth(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    gboolean sm = !g_variant_get_boolean(g_action_get_state(G_ACTION(a)));
    (void)p;
    h89session_set_int(self->session, "view.smooth", sm ? 1 : 0);
    h89_window_apply_smooth(self, sm);
}

void h89_window_apply_aspect(H89Window *self, int aspect)
{
    GAction *a = g_action_map_lookup_action(G_ACTION_MAP(self), "tv-aspect");
    h89_display_set_tv_aspect(H89_DISPLAY(self->display), aspect == 0);
    if (a)
        g_simple_action_set_state(G_SIMPLE_ACTION(a), g_variant_new_boolean(aspect == 0));
}

void h89_window_apply_smooth(H89Window *self, int smooth)
{
    GAction *a = g_action_map_lookup_action(G_ACTION_MAP(self), "smooth");
    h89_display_set_smooth(H89_DISPLAY(self->display), smooth != 0);
    if (a)
        g_simple_action_set_state(G_SIMPLE_ACTION(a), g_variant_new_boolean(smooth != 0));
}

/* ---- FujiNet -------------------------------------------------------------------------- */

static void action_fujinet_config(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    if (!h89session_fujinet_running(self->session)) {
        h89_window_toast(self, "FujiNet is not running");
        return;
    }
    h89_fujiconfig_show(GTK_WINDOW(self), self->session);
}

static void action_fujinet_log(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    h89_fujilog_show(GTK_WINDOW(self), self->session);
}

static void action_fujinet_restart(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    (void)a; (void)p;
    if (h89session_fujinet_restart(self->session) != 0)
        show_message(GTK_WIDGET(self), "Cannot Restart FujiNet",
                     h89session_last_error(self->session));
    else
        h89_window_toast(self, "FujiNet restarted");
}

static void action_about(GSimpleAction *a, GVariant *p, gpointer user_data)
{
    H89Window *self = user_data;
    AdwDialog *about;
    (void)a; (void)p;
    about = adw_about_dialog_new();
    adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(about), "FujiNet Go H89");
    adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), h89_icon_name());
    adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), H89_VERSION_STRING);
    adw_about_dialog_set_developer_name(ADW_ABOUT_DIALOG(about), "Thomas Cherryhomes");
    adw_about_dialog_set_website(ADW_ABOUT_DIALOG(about), "https://fujinet.online/");
    adw_about_dialog_set_issue_url(ADW_ABOUT_DIALOG(about),
        "https://github.com/tschak909/fujinet-go-h89-desktop/issues");
    adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about), GTK_LICENSE_GPL_3_0);
    adw_about_dialog_set_comments(ADW_ABOUT_DIALOG(about),
        "A Heathkit H89 with a FujiNet card. The emulator is VirtualH89 "
        "(BSD-3-Clause, Mark Garlanger and Douglas Miller), adapted, with "
        "the FujiNet firmware (GPL-3.0-or-later) as the card's ESP32. The "
        "Heath ROMs are not included: they are imported from MAME's sets.");
    adw_dialog_present(about, GTK_WIDGET(self));
}

static const GActionEntry win_actions[] = {
    { "reset", action_reset, NULL, NULL, NULL, { 0 } },
    { "power-cycle", action_power_cycle, NULL, NULL, NULL, { 0 } },
    { "warp", action_warp, NULL, "false", NULL, { 0 } },
    { "import-roms", action_import_roms, NULL, NULL, NULL, { 0 } },
    { "prefs", action_prefs, NULL, NULL, NULL, { 0 } },
    { "quit", action_quit, NULL, NULL, NULL, { 0 } },
    { "drive-insert", action_drive_insert, "i", NULL, NULL, { 0 } },
    { "drive-eject", action_drive_eject, "i", NULL, NULL, { 0 } },
    { "insert-disk", action_insert_disk, NULL, NULL, NULL, { 0 } },
    { "new-disk", action_new_disk, NULL, NULL, NULL, { 0 } },
    { "disks-folder", action_disks_folder, NULL, NULL, NULL, { 0 } },
    { "copy-screen", action_copy_screen, NULL, NULL, NULL, { 0 } },
    { "paste", action_paste, NULL, NULL, NULL, { 0 } },
    { "keys", action_keys, NULL, NULL, NULL, { 0 } },
    { "debugger", action_debugger, NULL, NULL, NULL, { 0 } },
    { "fullscreen", action_fullscreen, NULL, NULL, NULL, { 0 } },
    { "tv-aspect", action_aspect, NULL, "true", NULL, { 0 } },
    { "smooth", action_smooth, NULL, "true", NULL, { 0 } },
    { "fujinet-config", action_fujinet_config, NULL, NULL, NULL, { 0 } },
    { "fujinet-log", action_fujinet_log, NULL, NULL, NULL, { 0 } },
    { "fujinet-restart", action_fujinet_restart, NULL, NULL, NULL, { 0 } },
    { "about", action_about, NULL, NULL, NULL, { 0 } },
};

/* ---- construction ---------------------------------------------------------------------- */

static GMenu *build_menu(H89Window *self)
{
    GMenu *menu = g_menu_new();
    GMenu *machine = g_menu_new();
    GMenu *edit = g_menu_new();
    GMenu *view = g_menu_new();
    GMenu *fuji = g_menu_new();
    GMenu *app = g_menu_new();

    g_menu_append(machine, "_Reset (SHIFT+RESET)", "win.reset");
    g_menu_append(machine, "_Power Cycle", "win.power-cycle");
    g_menu_append(machine, "_Warp Speed", "win.warp");
    g_menu_append(machine, "Import System _ROMs\xe2\x80\xa6", "win.import-roms");
    g_menu_append_section(menu, "Machine", G_MENU_MODEL(machine));

    if (!self->drives_menu)
        self->drives_menu = g_menu_new();
    g_menu_append_submenu(menu, "_Drives", G_MENU_MODEL(self->drives_menu));

    g_menu_append(edit, "_Copy Screen Text", "win.copy-screen");
    g_menu_append(edit, "P_aste", "win.paste");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(edit));

    g_menu_append(view, "H19 _Keys (F9)", "win.keys");
    g_menu_append(view, "De_bugger (F12)", "win.debugger");
    g_menu_append(view, "_TV Aspect", "win.tv-aspect");
    g_menu_append(view, "S_mooth Scaling", "win.smooth");
    g_menu_append(view, "_Fullscreen (F11)", "win.fullscreen");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(view));

    g_menu_append(fuji, "FujiNet _Configuration", "win.fujinet-config");
    g_menu_append(fuji, "Console _Log", "win.fujinet-log");
    g_menu_append(fuji, "R_estart FujiNet", "win.fujinet-restart");
    g_menu_append_section(menu, "FujiNet", G_MENU_MODEL(fuji));

    g_menu_append(app, "_Preferences", "win.prefs");
    g_menu_append(app, "_About FujiNet Go H89", "win.about");
    g_menu_append(app, "_Quit", "win.quit");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(app));

    g_object_unref(machine);
    g_object_unref(edit);
    g_object_unref(view);
    g_object_unref(fuji);
    g_object_unref(app);
    return menu;
}

static void h89_window_dispose(GObject *object)
{
    H89Window *self = H89_WINDOW(object);
    if (self->status_id) {
        g_source_remove(self->status_id);
        self->status_id = 0;
    }
    if (self->fast_id) {
        g_source_remove(self->fast_id);
        self->fast_id = 0;
    }
    if (self->flash_id) {
        g_source_remove(self->flash_id);
        self->flash_id = 0;
    }
    g_clear_object(&self->drives_menu);
    G_OBJECT_CLASS(h89_window_parent_class)->dispose(object);
}

static void h89_window_class_init(H89WindowClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = h89_window_dispose;
}

static void h89_window_init(H89Window *self)
{
    (void)self;
}

static GtkWidget *dot(const char *tip)
{
    GtkWidget *d = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(d, "h89-dot");
    gtk_widget_add_css_class(d, "h89-dot-off");
    gtk_widget_set_valign(d, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(d, tip);
    return d;
}

GtkWidget *h89_window_new(AdwApplication *app, h89session *session)
{
    H89Window *self = g_object_new(H89_TYPE_WINDOW, "application", app, NULL);
    GtkWidget *header, *menu_button, *toolbar, *status_box;
    GtkEventController *keys;

    self->session = session;
    h89_install_accent_css();

    gtk_window_set_title(GTK_WINDOW(self), "FujiNet Go H89");
    gtk_window_set_icon_name(GTK_WINDOW(self), h89_icon_name());
    /* the H19's 640 x 500 at 4:3, half again: 1000 x 750 */
    gtk_window_set_default_size(GTK_WINDOW(self), 1000, 750 + 46);

    g_action_map_add_action_entries(G_ACTION_MAP(self), win_actions, G_N_ELEMENTS(win_actions),
                                    self);
    {
        /* CTRL+SHIFT: CTRL alone is the H19's control codes */
        static const char *const prefs[] = { "<Control>comma", NULL };
        static const char *const quit[] = { "<Control><Shift>q", NULL };
        static const char *const reset[] = { "<Control><Shift>r", NULL };
        static const char *const copy[] = { "<Control><Shift>c", NULL };
        static const char *const paste[] = { "<Control><Shift>v", NULL };
        static const char *const insert[] = { "<Control><Shift>o", NULL };
        GtkApplication *a = GTK_APPLICATION(app);
        gtk_application_set_accels_for_action(a, "win.prefs", prefs);
        gtk_application_set_accels_for_action(a, "win.quit", quit);
        gtk_application_set_accels_for_action(a, "win.reset", reset);
        gtk_application_set_accels_for_action(a, "win.copy-screen", copy);
        gtk_application_set_accels_for_action(a, "win.paste", paste);
        gtk_application_set_accels_for_action(a, "win.insert-disk", insert);
    }

    header = adw_header_bar_new();
    menu_button = gtk_menu_button_new();
    self->menu_button = menu_button;
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_menu_button_set_primary(GTK_MENU_BUTTON(menu_button), TRUE);
    gtk_widget_set_tooltip_text(menu_button, "Main Menu");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), menu_button);

    status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    self->status_dot = dot("FujiNet link");
    self->disk_dot = dot("Drive activity");
    self->status = gtk_label_new("Starting\xe2\x80\xa6");
    gtk_widget_add_css_class(self->status, "dim-label");
    gtk_label_set_ellipsize(GTK_LABEL(self->status), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(status_box), self->disk_dot);
    gtk_box_append(GTK_BOX(status_box), self->status_dot);
    gtk_box_append(GTK_BOX(status_box), self->status);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header), status_box);

    self->display = h89_display_new(session);
    h89_window_apply_aspect(self, h89session_get_int(session, "view.aspect", 0));
    h89_window_apply_smooth(self, h89session_get_int(session, "view.smooth", 1));
    h89session_apply_view(session);
    {
        GtkDropTarget *drop = gtk_drop_target_new(G_TYPE_FILE, GDK_ACTION_COPY);
        g_signal_connect(drop, "drop", G_CALLBACK(on_drop), self);
        gtk_widget_add_controller(self->display, GTK_EVENT_CONTROLLER(drop));
    }

    self->toast_overlay = adw_toast_overlay_new();
    adw_toast_overlay_set_child(ADW_TOAST_OVERLAY(self->toast_overlay), self->display);
    toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), self->toast_overlay);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self), toolbar);

    /* capture on the WINDOW so typing works whatever has focus */
    keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self), keys);

    {
        GAction *w = g_action_map_lookup_action(G_ACTION_MAP(self), "warp");
        g_simple_action_set_state(G_SIMPLE_ACTION(w),
            g_variant_new_boolean(h89session_get_int(session, "machine.warp", 0) != 0));
    }

    rebuild_drives_menu(self, TRUE);
    self->status_id = g_timeout_add_seconds(1, status_tick, self);
    self->fast_id = g_timeout_add(250, fast_tick, self);
    update_status(self);

    /* H89_OPEN_KEYS=1 / H89_OPEN_DEBUGGER=1 / H89_OPEN_SETTINGS=1 open those
     * windows at launch, the family's way in when the app misbehaves before
     * the menu is reachable. */
    {
        const char *env = g_getenv("H89_OPEN_KEYS");
        if (env && *env && *env != '0')
            h89_keys_panel_toggle(GTK_WINDOW(self), session);
        env = g_getenv("H89_OPEN_DEBUGGER");
        if (env && *env && *env != '0')
            h89_debugger_show(GTK_WINDOW(self), session);
        env = g_getenv("H89_OPEN_SETTINGS");
        if (env && *env && *env != '0')
            h89_prefs_show(self, session);
    }
    return GTK_WIDGET(self);
}
