/*
 * The Preferences dialog: a programmatic AdwPreferencesDialog, no .ui file.
 *
 * Pages: Machine (model, memory, the monitor and the ROMs), Slots & Drives,
 * Switches (SW501 as the chosen monitor reads it, the H19's SW401 and
 * SW402), CP/NET, Display & Sound, FujiNet. The core owns what the choices
 * are and which combinations make sense (h89config.h); this file only lays
 * them out. The machine's configuration is a local copy until the dialog
 * closes; a line at the top of the Machine page says whether it can be
 * built as it stands.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "prefs.h"

#include "fujilog.h"

#include <stdlib.h>
#include <string.h>

#define MAX_MONITORS 16
#define MAX_SW_ROWS  12

typedef struct {
    H89Window *window;
    h89session *session;
    AdwDialog *dialog;
    h89_config cfg;            /* what the dialog shows and edits */
    h89_config orig;           /* the settings when it opened */
    gboolean host_dirty;       /* FujiNet or audio switched */

    GtkWidget *status_row;
    GtkWidget *roms_row;
    GtkWidget *monitor_row;
    int monitors[MAX_MONITORS];
    int nmonitors;

    GtkWidget *h17_group, *h37_group, *mms316_group, *mms320_group, *fuji_ack_row;
    GtkWidget *sw501_group;
    GtkWidget *sw501_rows[MAX_SW_ROWS];
    int sw501_count;
    GtkWidget *cpnet_rows[3];
    gboolean syncing;
    gboolean in_sw501;         /* an SW501 row is changing: do not rebuild them */
} PrefsState;

static void config_changed(PrefsState *st);
static void rebuild_sw501(PrefsState *st);

/* ---- rows bound to the configuration ---------------------------------------------- */

typedef struct {
    PrefsState *st;
    int *field;                /* into st->cfg */
    const int *values;         /* the choice's value per position, or NULL: the position */
    int nvalues;
    int rebuild;               /* the SW501 rows follow (the monitor changed) */
} IntBinding;

static void binding_free(gpointer p, GClosure *c)
{
    (void)c;
    g_free(p);
}

static void int_combo_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    IntBinding *b = user_data;
    guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    (void)pspec;
    if (b->st->syncing || sel == GTK_INVALID_LIST_POSITION)
        return;
    *b->field = b->values ? (sel < (guint)b->nvalues ? b->values[sel] : *b->field) : (int)sel;
    config_changed(b->st);
}

/* A combo over `names` (NULL-terminated or a name function) for an int
 * field; `values` maps positions to values when they are not 0..n-1. */
static GtkWidget *int_combo(PrefsState *st, const char *title, const char *subtitle, int *field,
                            const char *(*name_fn)(int), const char *const *names,
                            const int *values, int nvalues)
{
    GtkWidget *row = adw_combo_row_new();
    GtkStringList *model = gtk_string_list_new(NULL);
    IntBinding *b = g_new0(IntBinding, 1);
    guint sel = 0;
    int n = 0;

    for (int i = 0; name_fn ? name_fn(i) != NULL : names[i] != NULL; i++, n++)
        gtk_string_list_append(model, name_fn ? name_fn(i) : names[i]);
    if (values) {
        for (int i = 0; i < nvalues; i++)
            if (values[i] == *field)
                sel = (guint)i;
    } else if (*field >= 0 && *field < n) {
        sel = (guint)*field;
    }
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), sel);
    b->st = st;
    b->field = field;
    b->values = values;
    b->nvalues = nvalues;
    g_signal_connect_data(row, "notify::selected", G_CALLBACK(int_combo_changed), b,
                          binding_free, 0);
    g_object_unref(model);
    return row;
}

/* A bit of a switch byte. */
typedef struct {
    PrefsState *st;
    uint8_t *byte;
    uint8_t mask;
} BitBinding;

static void bit_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    BitBinding *b = user_data;
    (void)pspec;
    if (b->st->syncing)
        return;
    if (adw_switch_row_get_active(ADW_SWITCH_ROW(row)))
        *b->byte |= b->mask;
    else
        *b->byte &= (uint8_t)~b->mask;
    config_changed(b->st);
}

static GtkWidget *bit_row(PrefsState *st, const char *title, const char *subtitle, uint8_t *byte,
                          uint8_t mask)
{
    GtkWidget *row = adw_switch_row_new();
    BitBinding *b = g_new0(BitBinding, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    adw_switch_row_set_active(ADW_SWITCH_ROW(row), (*byte & mask) != 0);
    b->st = st;
    b->byte = byte;
    b->mask = mask;
    g_signal_connect_data(row, "notify::active", G_CALLBACK(bit_changed), b, binding_free, 0);
    return row;
}

/* An int field that is a yes/no. */
static void flag_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    IntBinding *b = user_data;
    (void)pspec;
    if (b->st->syncing)
        return;
    *b->field = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) ? 1 : 0;
    config_changed(b->st);
}

static GtkWidget *flag_row(PrefsState *st, const char *title, const char *subtitle, int *field)
{
    GtkWidget *row = adw_switch_row_new();
    IntBinding *b = g_new0(IntBinding, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    adw_switch_row_set_active(ADW_SWITCH_ROW(row), *field != 0);
    b->st = st;
    b->field = field;
    g_signal_connect_data(row, "notify::active", G_CALLBACK(flag_changed), b, binding_free, 0);
    return row;
}

static void spin_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    IntBinding *b = user_data;
    (void)pspec;
    if (b->st->syncing)
        return;
    *b->field = (int)adw_spin_row_get_value(ADW_SPIN_ROW(row));
    config_changed(b->st);
}

static GtkWidget *spin_row(PrefsState *st, const char *title, const char *subtitle, int *field,
                           int lo, int hi)
{
    GtkWidget *row = adw_spin_row_new_with_range(lo, hi, 1);
    IntBinding *b = g_new0(IntBinding, 1);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    adw_spin_row_set_value(ADW_SPIN_ROW(row), *field);
    b->st = st;
    b->field = field;
    g_signal_connect_data(row, "notify::value", G_CALLBACK(spin_changed), b, binding_free, 0);
    return row;
}

/* ---- rows bound to the settings store (live, or a restart) ---------------------------- */

enum { SET_LIVE_VIEW, SET_LIVE, SET_HOST, SET_ASPECT, SET_SMOOTH };

typedef struct {
    PrefsState *st;
    const char *key;
    int def;
    int kind;
} SetBinding;

static void setting_apply(SetBinding *b, int v)
{
    h89session_set_int(b->st->session, b->key, v);
    switch (b->kind) {
    case SET_LIVE_VIEW:
        h89session_apply_view(b->st->session);
        break;
    case SET_HOST:
        b->st->host_dirty = TRUE;
        break;
    case SET_ASPECT:
        h89_window_apply_aspect(b->st->window, v);
        break;
    case SET_SMOOTH:
        h89_window_apply_smooth(b->st->window, v);
        break;
    default:
        break;
    }
}

static void set_switch_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    SetBinding *b = user_data;
    int v = adw_switch_row_get_active(ADW_SWITCH_ROW(row)) ? 1 : 0;
    (void)pspec;
    if (h89session_get_int(b->st->session, b->key, b->def) != v)
        setting_apply(b, v);
}

static void set_combo_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    SetBinding *b = user_data;
    int v = (int)adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    (void)pspec;
    if (h89session_get_int(b->st->session, b->key, b->def) != v)
        setting_apply(b, v);
}

static SetBinding *set_binding(PrefsState *st, const char *key, int def, int kind)
{
    SetBinding *b = g_new0(SetBinding, 1);
    b->st = st;
    b->key = key;
    b->def = def;
    b->kind = kind;
    return b;
}

static GtkWidget *set_switch(PrefsState *st, const char *title, const char *subtitle,
                             const char *key, int def, int kind)
{
    GtkWidget *row = adw_switch_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    adw_switch_row_set_active(ADW_SWITCH_ROW(row), h89session_get_int(st->session, key, def));
    g_signal_connect_data(row, "notify::active", G_CALLBACK(set_switch_changed),
                          set_binding(st, key, def, kind), binding_free, 0);
    return row;
}

static GtkWidget *set_combo(PrefsState *st, const char *title, const char *subtitle,
                            const char *key, int def, const char *const *names, int kind)
{
    GtkWidget *row = adw_combo_row_new();
    GtkStringList *model = gtk_string_list_new(names);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
    adw_combo_row_set_selected(ADW_COMBO_ROW(row), (guint)h89session_get_int(st->session, key, def));
    g_signal_connect_data(row, "notify::selected", G_CALLBACK(set_combo_changed),
                          set_binding(st, key, def, kind), binding_free, 0);
    g_object_unref(model);
    return row;
}

static GtkWidget *info_row(const char *title, const char *subtitle)
{
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle)
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    return row;
}

static AdwPreferencesGroup *group(const char *title, const char *description)
{
    AdwPreferencesGroup *g = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(g, title);
    if (description)
        adw_preferences_group_set_description(g, description);
    return g;
}

/* ---- the state of the whole ------------------------------------------------------------- */

static gboolean has_card(const h89_config *c, int card)
{
    for (int i = 0; i < H89_SLOTS; i++)
        if (c->slot[i] == card)
            return TRUE;
    return FALSE;
}

static void config_changed(PrefsState *st)
{
    char why[256];
    gboolean ok = h89_config_validate(&st->cfg, why, sizeof why);

    adw_action_row_set_subtitle(ADW_ACTION_ROW(st->status_row),
        ok ? (memcmp(&st->cfg, &st->orig, sizeof st->cfg) == 0
                  ? "As it is now"
                  : "Changed: applied with a power cycle when Preferences closes")
           : why);
    if (ok)
        gtk_widget_remove_css_class(st->status_row, "error");
    else
        gtk_widget_add_css_class(st->status_row, "error");

    gtk_widget_set_visible(st->h17_group, has_card(&st->cfg, H89_CARD_H17));
    gtk_widget_set_visible(st->h37_group, has_card(&st->cfg, H89_CARD_H37));
    gtk_widget_set_visible(st->mms316_group, has_card(&st->cfg, H89_CARD_MMS77316));
    gtk_widget_set_visible(st->mms320_group, has_card(&st->cfg, H89_CARD_MMS77320));
    gtk_widget_set_visible(st->fuji_ack_row, has_card(&st->cfg, H89_CARD_FUJINET));
    for (int i = 0; i < 3; i++)
        gtk_widget_set_sensitive(st->cpnet_rows[i], st->cfg.cpnet_enabled != 0);
    rebuild_sw501(st);
}

/* ---- Machine -------------------------------------------------------------------------------- */

static void refresh_roms(PrefsState *st)
{
    char what[512], sub[700];
    int missing = h89session_missing_roms(st->session, what, sizeof what);
    g_snprintf(sub, sizeof sub, missing ? "Still needed: %s" : "Everything this machine needs "
               "is imported%s", missing ? what : "");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(st->roms_row), sub);
}

static void monitor_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    PrefsState *st = user_data;
    guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    const h89_rom_info *r;
    (void)pspec;
    if (st->syncing || sel >= (guint)st->nmonitors)
        return;
    r = h89_rom_at(st->monitors[sel]);
    if (!r || strcmp(st->cfg.monitor, r->file) == 0)
        return;
    g_strlcpy(st->cfg.monitor, r->file, sizeof st->cfg.monitor);
    config_changed(st);
}

static void fill_monitors(PrefsState *st)
{
    GtkStringList *model = gtk_string_list_new(NULL);
    guint sel = 0;
    gboolean found = FALSE;

    st->nmonitors = h89session_monitors(st->session, st->monitors, MAX_MONITORS);
    for (int i = 0; i < st->nmonitors; i++) {
        const h89_rom_info *r = h89_rom_at(st->monitors[i]);
        char text[200];
        g_snprintf(text, sizeof text, "%s%s%s", r->desc, r->needs ? " \xe2\x80\x94 needs " : "",
                   r->needs ? r->needs : "");
        gtk_string_list_append(model, text);
        if (strcmp(r->file, st->cfg.monitor) == 0) {
            sel = (guint)i;
            found = TRUE;
        }
    }
    if (!found && st->cfg.monitor[0] && st->nmonitors < MAX_MONITORS) {
        int idx = h89_rom_find_file(st->cfg.monitor);
        const h89_rom_info *r = idx >= 0 ? h89_rom_at(idx) : NULL;
        char text[200];
        g_snprintf(text, sizeof text, "%s (not imported)", r ? r->desc : st->cfg.monitor);
        gtk_string_list_append(model, text);
        st->monitors[st->nmonitors] = idx;
        sel = (guint)st->nmonitors++;
    }
    st->syncing = TRUE;
    adw_combo_row_set_model(ADW_COMBO_ROW(st->monitor_row), G_LIST_MODEL(model));
    adw_combo_row_set_selected(ADW_COMBO_ROW(st->monitor_row), sel);
    st->syncing = FALSE;
    g_object_unref(model);
}

static void on_roms_file(GObject *src, GAsyncResult *res, gpointer user_data)
{
    AdwDialog *dialog = user_data;
    PrefsState *st = g_object_get_data(G_OBJECT(dialog), "prefs-state");
    g_autoptr(GListModel) files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(src), res, NULL);
    if (st && files) {
        for (guint i = 0; i < g_list_model_get_n_items(files); i++) {
            g_autoptr(GFile) f = g_list_model_get_item(files, i);
            g_autofree char *path = g_file_get_path(f);
            if (path)
                h89_show_rom_import(GTK_WIDGET(dialog), st->session, path);
        }
        fill_monitors(st);
        refresh_roms(st);
    }
    g_object_unref(dialog);
}

static void on_import_roms(GtkButton *b, gpointer user_data)
{
    PrefsState *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new();
    (void)b;
    gtk_file_dialog_set_title(dlg, "Import System ROMs");
    gtk_file_dialog_open_multiple(dlg, GTK_WINDOW(st->window), NULL, on_roms_file,
                                  g_object_ref(st->dialog));
    g_object_unref(dlg);
}

static const int SPEEDUPS[] = { 0, 2, 3, 4, 6, 8, 10, 16, 20, 40 };
static const char *const SPEEDUP_NAMES[] = { "None (2.048 MHz)", "2\xc3\x97", "3\xc3\x97",
    "4\xc3\x97", "6\xc3\x97", "8\xc3\x97", "10\xc3\x97", "16\xc3\x97", "20\xc3\x97",
    "40\xc3\x97", NULL };

static AdwPreferencesPage *machine_page(PrefsState *st)
{
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    AdwPreferencesGroup *state = group(NULL, NULL);
    AdwPreferencesGroup *machine = group("Machine",
        "The CPU board as it would be strapped and stuffed");
    AdwPreferencesGroup *roms = group("System ROMs", NULL);
    GtkWidget *import;

    adw_preferences_page_set_title(page, "Machine");
    adw_preferences_page_set_name(page, "machine");
    adw_preferences_page_set_icon_name(page, "computer-symbolic");

    st->status_row = info_row("Configuration", NULL);
    adw_preferences_group_add(state, st->status_row);

    adw_preferences_group_add(machine, int_combo(st, "Model",
        "The H89's ORG-0 decoder lets CP/M put RAM at 0; the H88 has none",
        &st->cfg.model, h89_model_name, NULL, NULL, 0));
    adw_preferences_group_add(machine, int_combo(st, "Memory", "On the CPU board",
        &st->cfg.memory, h89_memory_name, NULL, NULL, 0));
    adw_preferences_group_add(machine, int_combo(st, "Memory slot P503", NULL, &st->cfg.p503,
        h89_p503_name, NULL, NULL, 0));
    st->monitor_row = adw_combo_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(st->monitor_row), "Monitor ROM");
    adw_combo_row_set_use_subtitle(ADW_COMBO_ROW(st->monitor_row), TRUE);
    g_signal_connect(st->monitor_row, "notify::selected", G_CALLBACK(monitor_changed), st);
    adw_preferences_group_add(machine, st->monitor_row);
    fill_monitors(st);
    adw_preferences_group_add(machine, int_combo(st, "Speed-up",
        "A faster Z80; the disks and the clock keep their own time (not with the MMS 77318)",
        &st->cfg.speedup, NULL, SPEEDUP_NAMES, SPEEDUPS, G_N_ELEMENTS(SPEEDUPS)));

    st->roms_row = info_row("Imported ROMs", NULL);
    adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(st->roms_row), TRUE);
    import = gtk_button_new_with_label("Import\xe2\x80\xa6");
    gtk_widget_set_valign(import, GTK_ALIGN_CENTER);
    g_signal_connect(import, "clicked", G_CALLBACK(on_import_roms), st);
    adw_action_row_add_suffix(ADW_ACTION_ROW(st->roms_row), import);
    adw_preferences_group_add(roms, st->roms_row);
    refresh_roms(st);

    adw_preferences_page_add(page, state);
    adw_preferences_page_add(page, machine);
    adw_preferences_page_add(page, roms);
    return page;
}

/* ---- Slots & Drives ----------------------------------------------------------------------------- */

static const int MMS320_PORTS[] = { 0x78, 0x7c };
static const char *const MMS320_PORT_NAMES[] = { "170Q (0x78, JP1A)", "174Q (0x7C, JP1B)", NULL };
static const int MMS320_INTS[] = { 0, 3, 4, 5 };
static const char *const MMS320_INT_NAMES[] = { "None", "INT 3 (JP2A)", "INT 4 (JP2B)",
                                                "INT 5 (JP2C)", NULL };

static AdwPreferencesPage *slots_page(PrefsState *st)
{
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    AdwPreferencesGroup *slots = group("I/O Slots",
        "The cards on the right-side bus. Each is fitted once; their ports may not overlap.");
    AdwPreferencesGroup *h17 = group("H-88-1 (H17) Drives", "Hard-sectored 5.25\"");
    AdwPreferencesGroup *h37 = group("Z-89-37 Drives", "Soft-sectored, single and double density");
    AdwPreferencesGroup *m316 = group("MMS 77316 Drives", "Units 1\xe2\x80\x93" "4 are 8\", 5\xe2\x80\x93" "8 are 5.25\"");
    AdwPreferencesGroup *m320 = group("MMS 77320 SASI", "Hard disk units: files in the app's sasi folder");
    static const char *const slot_names[] = { "P504", "P505", "P506" };
    char title[64];

    adw_preferences_page_set_title(page, "Slots & Drives");
    adw_preferences_page_set_name(page, "slots");
    adw_preferences_page_set_icon_name(page, "drive-harddisk-symbolic");

    for (int i = 0; i < H89_SLOTS; i++)
        adw_preferences_group_add(slots, int_combo(st, slot_names[i], NULL, &st->cfg.slot[i],
                                                   h89_card_name, NULL, NULL, 0));
    for (int i = 0; i < H89_H17_DRIVES; i++) {
        g_snprintf(title, sizeof title, "Drive %d", i + 1);
        adw_preferences_group_add(h17, int_combo(st, title, NULL, &st->cfg.h17_drive[i],
                                                 h89_h17_drive_name, NULL, NULL, 0));
    }
    for (int i = 0; i < H89_H37_DRIVES; i++) {
        g_snprintf(title, sizeof title, "Drive %d", i + 1);
        adw_preferences_group_add(h37, int_combo(st, title, NULL, &st->cfg.h37_drive[i],
                                                 h89_fdd_name, NULL, NULL, 0));
    }
    for (int i = 0; i < H89_MMS316_DRIVES; i++) {
        g_snprintf(title, sizeof title, "Unit %d (%s)", i + 1, i < 4 ? "8\"" : "5.25\"");
        adw_preferences_group_add(m316, int_combo(st, title, NULL, &st->cfg.mms316_drive[i],
                                                  h89_fdd_name, NULL, NULL, 0));
    }
    adw_preferences_group_add(m320, int_combo(st, "Port", NULL, &st->cfg.mms320_port, NULL,
                                              MMS320_PORT_NAMES, MMS320_PORTS, 2));
    adw_preferences_group_add(m320, int_combo(st, "Interrupt", NULL, &st->cfg.mms320_intr, NULL,
                                              MMS320_INT_NAMES, MMS320_INTS, 4));
    for (int i = 0; i < H89_MMS320_DRIVES; i++) {
        g_snprintf(title, sizeof title, "Unit %d", i);
        adw_preferences_group_add(m320, int_combo(st, title, NULL, &st->cfg.mms320_drive[i],
                                                  h89_sasi_name, NULL, NULL, 0));
    }
    st->h17_group = GTK_WIDGET(h17);
    st->h37_group = GTK_WIDGET(h37);
    st->mms316_group = GTK_WIDGET(m316);
    st->mms320_group = GTK_WIDGET(m320);

    adw_preferences_page_add(page, slots);
    adw_preferences_page_add(page, h17);
    adw_preferences_page_add(page, h37);
    adw_preferences_page_add(page, m316);
    adw_preferences_page_add(page, m320);
    return page;
}

/* ---- Switches ----------------------------------------------------------------------------------- */

typedef struct {
    PrefsState *st;
    int field;
} SwField;

static void sw501_field_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    SwField *f = user_data;
    PrefsState *st = f->st;
    guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    uint8_t mask = h89_sw501_field_mask(st->cfg.monitor, f->field);
    (void)pspec;
    if (st->syncing || sel == GTK_INVALID_LIST_POSITION)
        return;
    st->cfg.sw501 = (uint8_t)((st->cfg.sw501 & ~mask) |
                              h89_sw501_option_value(st->cfg.monitor, f->field, (int)sel));
    st->in_sw501 = TRUE;
    config_changed(st);
    st->in_sw501 = FALSE;
}

/* SW501's fields are the chosen monitor's; with "from the slots" on, the
 * device fields show what the slots make them and cannot be changed. */
static void rebuild_sw501(PrefsState *st)
{
    const char *mon = st->cfg.monitor;
    uint8_t eff = h89_config_effective_sw501(&st->cfg);
    uint8_t automask = st->cfg.sw501_auto ? h89_sw501_auto_mask(mon) : 0;
    int n = h89_sw501_field_count(mon);

    if (st->syncing || st->in_sw501)
        return;
    st->syncing = TRUE;
    for (int i = 0; i < st->sw501_count; i++)
        adw_preferences_group_remove(ADW_PREFERENCES_GROUP(st->sw501_group), st->sw501_rows[i]);
    st->sw501_count = 0;
    for (int i = 0; i < n && st->sw501_count < MAX_SW_ROWS; i++) {
        uint8_t mask = h89_sw501_field_mask(mon, i);
        GtkWidget *row = adw_combo_row_new();
        GtkStringList *model = gtk_string_list_new(NULL);
        SwField *f = g_new0(SwField, 1);
        guint sel = 0;
        for (int o = 0; o < h89_sw501_option_count(mon, i); o++) {
            gtk_string_list_append(model, h89_sw501_option_name(mon, i, o));
            if ((eff & mask) == h89_sw501_option_value(mon, i, o))
                sel = (guint)o;
        }
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), h89_sw501_field_name(mon, i));
        adw_combo_row_set_model(ADW_COMBO_ROW(row), G_LIST_MODEL(model));
        adw_combo_row_set_selected(ADW_COMBO_ROW(row), sel);
        gtk_widget_set_sensitive(row, (mask & automask) == 0);
        f->st = st;
        f->field = i;
        g_signal_connect_data(row, "notify::selected", G_CALLBACK(sw501_field_changed), f,
                              binding_free, 0);
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(st->sw501_group), row);
        st->sw501_rows[st->sw501_count++] = row;
        g_object_unref(model);
    }
    st->syncing = FALSE;
}

/* SW401's low nibble: the H19's baud rate (0 and 12-15 are 9600). */
static void baud_changed(GObject *row, GParamSpec *pspec, gpointer user_data)
{
    PrefsState *st = user_data;
    static const uint8_t codes[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    guint sel = adw_combo_row_get_selected(ADW_COMBO_ROW(row));
    (void)pspec;
    if (st->syncing || sel >= G_N_ELEMENTS(codes))
        return;
    st->cfg.sw401 = (uint8_t)((st->cfg.sw401 & 0xf0) | codes[sel]);
    config_changed(st);
}

static AdwPreferencesPage *switches_page(PrefsState *st)
{
    static const char *const bauds[] = { "110", "150", "300", "600", "1200", "1800", "2000",
                                         "2400", "3600", "4800", "7200", "9600", "19200", NULL };
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    AdwPreferencesGroup *sw501 = group("SW501 (CPU board)",
        "Read by the monitor ROM at power-up: the fields are the chosen monitor's");
    AdwPreferencesGroup *auto_g = group(NULL, NULL);
    AdwPreferencesGroup *sw401 = group("SW401 (H19: the line)", NULL);
    AdwPreferencesGroup *sw402 = group("SW402 (H19: the terminal)", NULL);
    GtkWidget *baud;
    guint sel;

    adw_preferences_page_set_title(page, "Switches");
    adw_preferences_page_set_name(page, "switches");
    adw_preferences_page_set_icon_name(page, "preferences-other-symbolic");

    adw_preferences_group_add(auto_g, flag_row(st, "Set the device switches from the slots",
        "The controllers the monitor boots from, as the cards in the slots need them",
        &st->cfg.sw501_auto));
    st->sw501_group = GTK_WIDGET(sw501);

    baud = adw_combo_row_new();
    {
        GtkStringList *model = gtk_string_list_new(bauds);
        int code = st->cfg.sw401 & 0x0f;
        sel = (code >= 1 && code <= 13) ? (guint)(code - 1) : 11;
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(baud), "Baud rate");
        adw_action_row_set_subtitle(ADW_ACTION_ROW(baud), "The console line: HDOS and CP/M "
                                    "find it at boot");
        adw_combo_row_set_model(ADW_COMBO_ROW(baud), G_LIST_MODEL(model));
        adw_combo_row_set_selected(ADW_COMBO_ROW(baud), sel);
        g_signal_connect(baud, "notify::selected", G_CALLBACK(baud_changed), st);
        g_object_unref(model);
    }
    adw_preferences_group_add(sw401, baud);
    adw_preferences_group_add(sw401, bit_row(st, "Parity", NULL, &st->cfg.sw401, 0x10));
    adw_preferences_group_add(sw401, bit_row(st, "Even parity", NULL, &st->cfg.sw401, 0x20));
    adw_preferences_group_add(sw401, bit_row(st, "Stick parity", NULL, &st->cfg.sw401, 0x40));
    adw_preferences_group_add(sw401, bit_row(st, "Full duplex", NULL, &st->cfg.sw401, 0x80));

    adw_preferences_group_add(sw402, bit_row(st, "Block cursor", NULL, &st->cfg.sw402, 0x01));
    adw_preferences_group_add(sw402, bit_row(st, "No key click", NULL, &st->cfg.sw402, 0x02));
    adw_preferences_group_add(sw402, bit_row(st, "Wrap at the end of a line", NULL, &st->cfg.sw402, 0x04));
    adw_preferences_group_add(sw402, bit_row(st, "Line feed on carriage return", NULL, &st->cfg.sw402, 0x08));
    adw_preferences_group_add(sw402, bit_row(st, "Carriage return on line feed", NULL, &st->cfg.sw402, 0x10));
    adw_preferences_group_add(sw402, bit_row(st, "ANSI mode", "At power-up; Heath mode otherwise",
                                             &st->cfg.sw402, 0x20));
    adw_preferences_group_add(sw402, bit_row(st, "Keypad shifted", NULL, &st->cfg.sw402, 0x40));
    adw_preferences_group_add(sw402, bit_row(st, "50 Hz refresh", NULL, &st->cfg.sw402, 0x80));

    adw_preferences_page_add(page, auto_g);
    adw_preferences_page_add(page, sw501);
    adw_preferences_page_add(page, sw401);
    adw_preferences_page_add(page, sw402);
    return page;
}

/* ---- CP/NET ------------------------------------------------------------------------------------------ */

static void on_cpnet_folder(GObject *src, GAsyncResult *res, gpointer user_data)
{
    AdwDialog *dialog = user_data;
    PrefsState *st = g_object_get_data(G_OBJECT(dialog), "prefs-state");
    g_autoptr(GFile) dir = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(src), res, NULL);
    g_autofree char *path = dir ? g_file_get_path(dir) : NULL;
    if (st && path) {
        g_strlcpy(st->cfg.cpnet_root, path, sizeof st->cfg.cpnet_root);
        adw_action_row_set_subtitle(ADW_ACTION_ROW(st->cpnet_rows[2]), path);
        config_changed(st);
    }
    g_object_unref(dialog);
}

static void on_choose_cpnet(GtkButton *b, gpointer user_data)
{
    PrefsState *st = user_data;
    GtkFileDialog *dlg = gtk_file_dialog_new();
    (void)b;
    gtk_file_dialog_set_title(dlg, "The CP/NET Host Folder");
    gtk_file_dialog_select_folder(dlg, GTK_WINDOW(st->window), NULL, on_cpnet_folder,
                                  g_object_ref(st->dialog));
    g_object_unref(dlg);
}

static AdwPreferencesPage *cpnet_page(PrefsState *st)
{
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    AdwPreferencesGroup *g = group("CP/NET",
        "A host folder served to CP/NET as its drives: a, b, ... are folders inside it");
    GtkWidget *choose;

    adw_preferences_page_set_title(page, "CP/NET");
    adw_preferences_page_set_name(page, "cpnet");
    adw_preferences_page_set_icon_name(page, "folder-remote-symbolic");
    adw_preferences_group_add(g, flag_row(st, "CP/NET device", NULL, &st->cfg.cpnet_enabled));
    st->cpnet_rows[0] = spin_row(st, "Port", "The device's I/O port", &st->cfg.cpnet_port, 0, 255);
    st->cpnet_rows[1] = spin_row(st, "Client ID", NULL, &st->cfg.cpnet_clientid, 0, 254);
    st->cpnet_rows[2] = info_row("Host folder", st->cfg.cpnet_root[0] ? st->cfg.cpnet_root
                                                                       : "(the default)");
    choose = gtk_button_new_with_label("Choose\xe2\x80\xa6");
    gtk_widget_set_valign(choose, GTK_ALIGN_CENTER);
    g_signal_connect(choose, "clicked", G_CALLBACK(on_choose_cpnet), st);
    adw_action_row_add_suffix(ADW_ACTION_ROW(st->cpnet_rows[2]), choose);
    for (int i = 0; i < 3; i++)
        adw_preferences_group_add(g, st->cpnet_rows[i]);
    adw_preferences_page_add(page, g);
    return page;
}

/* ---- Display & Sound ------------------------------------------------------------------------------- */

static void volume_changed(GtkRange *range, gpointer user_data)
{
    PrefsState *st = user_data;
    h89session_set_volume(st->session, (int)gtk_range_get_value(range));
}

static AdwPreferencesPage *display_page(PrefsState *st)
{
    static const char *const phosphors[] = { "White (P4)", "Green (P1)", "Amber (P3)", NULL };
    static const char *const aspects[] = { "The tube's 4:3", "Square pixels", NULL };
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    AdwPreferencesGroup *view = group("Display", NULL);
    AdwPreferencesGroup *sound = group("Sound", "The H19's bell and key click");

    adw_preferences_page_set_title(page, "Display & Sound");
    adw_preferences_page_set_name(page, "display");
    adw_preferences_page_set_icon_name(page, "video-display-symbolic");

    adw_preferences_group_add(view, set_combo(st, "Phosphor", NULL, "view.phosphor", 0,
                                              phosphors, SET_LIVE_VIEW));
    adw_preferences_group_add(view, set_switch(st, "Scanlines", "The gaps between the tube's lines",
                                               "view.scanlines", 0, SET_LIVE_VIEW));
    adw_preferences_group_add(view, set_switch(st, "Blinking cursor", NULL, "view.cursor_blink",
                                               1, SET_LIVE_VIEW));
    adw_preferences_group_add(view, set_combo(st, "Aspect", NULL, "view.aspect", 0, aspects,
                                              SET_ASPECT));
    adw_preferences_group_add(view, set_switch(st, "Smooth scaling", NULL, "view.smooth", 1,
                                               SET_SMOOTH));

    adw_preferences_group_add(sound, set_switch(st, "Enable audio", "Open the system audio device "
                                                "(applied when Preferences closes)",
                                                "enable_audio", 1, SET_HOST));
    {
        GtkWidget *row = adw_action_row_new();
        GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 5);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), "Volume");
        gtk_range_set_value(GTK_RANGE(scale), h89session_get_int(st->session, "sound.volume", 70));
        gtk_widget_set_size_request(scale, 200, -1);
        gtk_widget_set_valign(scale, GTK_ALIGN_CENTER);
        g_signal_connect(scale, "value-changed", G_CALLBACK(volume_changed), st);
        adw_action_row_add_suffix(ADW_ACTION_ROW(row), scale);
        adw_preferences_group_add(sound, row);
    }
    adw_preferences_group_add(sound, set_switch(st, "Bell", NULL, "sound.bell", 1, SET_LIVE));
    adw_preferences_group_add(sound, set_switch(st, "Key click", "Unless SW402 or ESC x2 turns it "
                                                "off", "sound.click", 1, SET_LIVE));
    adw_preferences_group_add(sound, set_switch(st, "Visual bell", "Flash the screen on a bell",
                                                "sound.visual_bell", 0, SET_LIVE));

    adw_preferences_page_add(page, view);
    adw_preferences_page_add(page, sound);
    return page;
}

/* ---- FujiNet ----------------------------------------------------------------------------------------------- */

static void on_open_webui(GtkButton *b, gpointer user_data)
{
    PrefsState *st = user_data;
    (void)b;
    if (!h89session_fujinet_running(st->session)) {
        adw_preferences_dialog_add_toast(ADW_PREFERENCES_DIALOG(st->dialog),
                                         adw_toast_new("FujiNet is not running"));
        return;
    }
    h89_fujiconfig_show(GTK_WINDOW(st->window), st->session);
}

static AdwPreferencesPage *fujinet_page(PrefsState *st)
{
    AdwPreferencesPage *page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    AdwPreferencesGroup *g = group("FujiNet",
        "The FujiNet runs inside the app; the H89's card talks to it");
    GtkWidget *row, *open;

    adw_preferences_page_set_title(page, "FujiNet");
    adw_preferences_page_set_name(page, "fujinet");
    adw_preferences_page_set_icon_name(page, "network-wireless-symbolic");
    adw_preferences_group_add(g, set_switch(st, "Enable FujiNet",
        "Off: the card's link stays down (applied when Preferences closes)",
        "enable_fujinet", 1, SET_HOST));
    row = info_row("Web UI", h89session_fujinet_webui_url(st->session));
    open = gtk_button_new_with_label("Open");
    gtk_widget_set_valign(open, GTK_ALIGN_CENTER);
    g_signal_connect(open, "clicked", G_CALLBACK(on_open_webui), st);
    adw_action_row_set_subtitle_selectable(ADW_ACTION_ROW(row), TRUE);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), open);
    adw_preferences_group_add(g, row);
    st->fuji_ack_row = spin_row(st, "Card handshake delay",
        "Cycles before the ESP32 acknowledges a byte the H89 writes", &st->cfg.fujinet_ack_delay,
        1, 10000);
    adw_preferences_group_add(g, st->fuji_ack_row);
    adw_preferences_page_add(page, g);
    return page;
}

/* ---- closing: apply ------------------------------------------------------------------------------------------- */

typedef struct {
    H89Window *window;
    gboolean host;
} RestartCtx;

static void on_power_cycle_answer(AdwAlertDialog *dlg, const char *response, gpointer user_data)
{
    RestartCtx *c = user_data;
    (void)dlg;
    if (g_strcmp0(response, "cycle") == 0)
        h89_window_restart(c->window, c->host);
    else if (c->host)
        h89_window_restart(c->window, TRUE);
    g_free(c);
}

static void prefs_closed(AdwDialog *dialog, gpointer user_data)
{
    PrefsState *st = user_data;
    char why[256];
    gboolean changed = memcmp(&st->cfg, &st->orig, sizeof st->cfg) != 0;
    gboolean running = h89session_is_running(st->session);

    g_object_set_data(G_OBJECT(dialog), "prefs-state", NULL);
    h89session_settings_flush(st->session);

    if (changed && !h89_config_validate(&st->cfg, why, sizeof why)) {
        AdwDialog *a = adw_alert_dialog_new("The Machine Was Not Changed", why);
        adw_alert_dialog_add_response(ADW_ALERT_DIALOG(a), "ok", "OK");
        adw_dialog_present(a, GTK_WIDGET(st->window));
        if (st->host_dirty)
            h89_window_restart(st->window, TRUE);
    } else if (changed) {
        int needs = running && h89session_config_needs_power_cycle(st->session, &st->cfg);
        h89session_set_config(st->session, &st->cfg);
        h89session_settings_flush(st->session);
        if (!running) {
            h89_window_start(st->window);
        } else if (needs) {
            RestartCtx *c = g_new0(RestartCtx, 1);
            AdwDialog *a = adw_alert_dialog_new("Power Cycle the H89?",
                "The new configuration takes effect when the H89 is switched off and "
                "on. The disks are written back first, and go back in their drives.");
            adw_alert_dialog_add_response(ADW_ALERT_DIALOG(a), "later", "Later");
            adw_alert_dialog_add_response(ADW_ALERT_DIALOG(a), "cycle", "Power Cycle");
            adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(a), "cycle",
                                                     ADW_RESPONSE_SUGGESTED);
            adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(a), "cycle");
            adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(a), "later");
            c->window = st->window;
            c->host = st->host_dirty;
            g_signal_connect(a, "response", G_CALLBACK(on_power_cycle_answer), c);
            adw_dialog_present(a, GTK_WIDGET(st->window));
        } else if (st->host_dirty) {
            h89_window_restart(st->window, TRUE);
        }
    } else if (st->host_dirty) {
        h89_window_restart(st->window, TRUE);
    }
    g_free(st);
}

void h89_prefs_show(H89Window *parent, h89session *session)
{
    PrefsState *st = g_new0(PrefsState, 1);
    AdwPreferencesDialog *dialog = ADW_PREFERENCES_DIALOG(adw_preferences_dialog_new());

    st->window = parent;
    st->session = session;
    st->dialog = ADW_DIALOG(dialog);
    h89session_get_config(session, &st->cfg);
    st->orig = st->cfg;
    g_object_set_data(G_OBJECT(dialog), "prefs-state", st);

    adw_dialog_set_title(ADW_DIALOG(dialog), "Preferences");
    adw_dialog_set_content_width(ADW_DIALOG(dialog), 640);
    adw_dialog_set_content_height(ADW_DIALOG(dialog), 640);
    adw_preferences_dialog_add(dialog, machine_page(st));
    adw_preferences_dialog_add(dialog, slots_page(st));
    adw_preferences_dialog_add(dialog, switches_page(st));
    adw_preferences_dialog_add(dialog, cpnet_page(st));
    adw_preferences_dialog_add(dialog, display_page(st));
    adw_preferences_dialog_add(dialog, fujinet_page(st));
    config_changed(st);
    /* H89_PREFS_PAGE=slots (switches, cpnet, display, fujinet) opens on that
     * page: for screenshots and for pointing someone at a setting */
    {
        const char *pg = g_getenv("H89_PREFS_PAGE");
        if (pg && *pg)
            adw_preferences_dialog_set_visible_page_name(dialog, pg);
    }

    g_signal_connect(dialog, "closed", G_CALLBACK(prefs_closed), st);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(parent));
}
