/*
 * keys_panel.c -- see keys_panel.h.
 *
 * The buttons never take the keyboard focus, and keys typed while the panel
 * is the active window go to the H89 as they would from the main window
 * (h89_window_key), so clicking ERASE and carrying on typing just works.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "keys_panel.h"

#include "window.h"

typedef struct {
    GtkWindow *win;
    GtkWindow *parent;
    h89session *session;
    GtkWidget *shift;            /* the latching SHIFT */
    GtkWidget *lights[8];
    guint timer;
} Panel;

static Panel *g_panel;

typedef struct {
    int key;
    const char *label;
    const char *tip;
} KeyDef;

static void on_key_clicked(GtkButton *b, gpointer user_data)
{
    Panel *p = user_data;
    int key = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "h89-key"));
    gboolean shift = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->shift));
    h89session_key(p->session, key, shift);
    /* a latched SHIFT is for one key, as on the keyboard's own */
    if (shift && key != H89K_RESET)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->shift), FALSE);
}

static GtkWidget *key_button(Panel *p, const KeyDef *k)
{
    GtkWidget *b = gtk_button_new_with_label(k->label);
    gtk_widget_set_focus_on_click(b, FALSE);
    gtk_widget_set_focusable(b, FALSE);
    if (k->tip)
        gtk_widget_set_tooltip_text(b, k->tip);
    g_object_set_data(G_OBJECT(b), "h89-key", GINT_TO_POINTER(k->key));
    g_signal_connect(b, "clicked", G_CALLBACK(on_key_clicked), p);
    return b;
}

static GtkWidget *key_row(Panel *p, const KeyDef *keys, int n)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    for (int i = 0; i < n; i++) {
        GtkWidget *b = key_button(p, &keys[i]);
        gtk_widget_set_hexpand(b, TRUE);
        gtk_box_append(GTK_BOX(box), b);
    }
    return box;
}

static const char *const LIGHT_NAMES[8] = {
    "Off line", "Hold screen", "Keypad shifted", "Alternate keypad",
    "Keyboard off", "Graphics", "Insert", "Reverse",
};

static gboolean refresh_lights(gpointer user_data)
{
    Panel *p = user_data;
    unsigned m = h89session_h19_modes(p->session);
    for (int i = 0; i < 8; i++) {
        GtkWidget *dot = p->lights[i];
        gboolean on = (m >> i) & 1;
        gtk_widget_remove_css_class(dot, on ? "h89-dot-off" : "h89-dot-on");
        gtk_widget_add_css_class(dot, on ? "h89-dot-on" : "h89-dot-off");
    }
    return G_SOURCE_CONTINUE;
}

static gboolean on_panel_key(GtkEventControllerKey *ctrl, guint keyval, guint keycode,
                             GdkModifierType state, gpointer user_data)
{
    Panel *p = user_data;
    (void)ctrl; (void)keycode;
    return h89_window_key(H89_WINDOW(p->parent), keyval, state);
}

static void on_destroy(GtkWidget *w, gpointer user_data)
{
    Panel *p = user_data;
    (void)w;
    if (p->timer)
        g_source_remove(p->timer);
    g_free(p);
    g_panel = NULL;
}

static GtkWidget *group(const char *title, GtkWidget *child)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_add_css_class(label, "heading");
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), child);
    return box;
}

void h89_keys_panel_toggle(GtkWindow *parent, h89session *session)
{
    static const KeyDef fkeys[] = {
        { H89K_F1, "f1", "ESC S" }, { H89K_F2, "f2", "ESC T" }, { H89K_F3, "f3", "ESC U" },
        { H89K_F4, "f4", "ESC V" }, { H89K_F5, "f5", "ESC W" },
        { H89K_ERASE, "ERASE", "ESC J (F6)" },
        { H89K_BLUE, "BLUE", "ESC P (F7)" }, { H89K_RED, "RED", "ESC Q (F8)" },
        { H89K_GRAY, "GRAY", "ESC R (F10)" },
    };
    static const KeyDef specials[] = {
        { 0x1b, "ESC", NULL }, { 0x09, "TAB", NULL }, { 0x08, "BACK SPACE", NULL },
        { 0x7f, "DELETE", NULL }, { H89K_LINEFEED, "LINE FEED", NULL },
        { 0x0d, "RETURN", NULL },
    };
    static const KeyDef terminal[] = {
        { H89K_BREAK, "BREAK", "A break on the line (Pause)" },
        { H89K_OFFLINE, "OFF LINE", "Keys go to the screen, not the computer" },
        { H89K_SCROLL, "SCROLL", "Releases hold-screen mode (Scroll Lock); with SHIFT, a page" },
        { H89K_RESET, "SHIFT+RESET", "Resets the H89: the CPU, its cards and the terminal" },
    };
    static const KeyDef keypad[] = {
        { H89K_KP7, "7\nIC", NULL }, { H89K_KP8, "8\n\xe2\x86\x91", NULL }, { H89K_KP9, "9\nDC", NULL },
        { H89K_KP4, "4\n\xe2\x86\x90", NULL }, { H89K_KP5, "5\nHOME", NULL }, { H89K_KP6, "6\n\xe2\x86\x92", NULL },
        { H89K_KP1, "1\nIL", NULL }, { H89K_KP2, "2\n\xe2\x86\x93", NULL }, { H89K_KP3, "3\nDL", NULL },
        { H89K_KP0, "0", NULL }, { H89K_KP_DOT, ".", NULL }, { H89K_KP_ENTER, "ENTER", NULL },
    };
    Panel *p;
    GtkWidget *box, *grid, *lights, *toolbar, *header;
    GtkEventController *keys;

    if (g_panel) {
        if (gtk_widget_get_visible(GTK_WIDGET(g_panel->win)))
            gtk_widget_set_visible(GTK_WIDGET(g_panel->win), FALSE);
        else
            gtk_window_present(g_panel->win);
        return;
    }

    p = g_new0(Panel, 1);
    p->parent = parent;
    p->session = session;
    p->win = GTK_WINDOW(adw_window_new());
    gtk_window_set_title(p->win, "H19 Keys");
    gtk_window_set_transient_for(p->win, parent);
    gtk_window_set_application(p->win, gtk_window_get_application(parent));
    gtk_window_set_hide_on_close(p->win, TRUE);
    gtk_window_set_resizable(p->win, FALSE);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 16);

    gtk_box_append(GTK_BOX(box), group("Function keys", key_row(p, fkeys, G_N_ELEMENTS(fkeys))));
    gtk_box_append(GTK_BOX(box), group("Keys", key_row(p, specials, G_N_ELEMENTS(specials))));

    {
        GtkWidget *row = key_row(p, terminal, G_N_ELEMENTS(terminal));
        p->shift = gtk_toggle_button_new_with_label("SHIFT");
        gtk_widget_set_focus_on_click(p->shift, FALSE);
        gtk_widget_set_focusable(p->shift, FALSE);
        gtk_widget_set_tooltip_text(p->shift,
            "Held for the next key: the keypad's functions, a page of SCROLL");
        gtk_box_prepend(GTK_BOX(row), p->shift);
        gtk_box_append(GTK_BOX(box), group("Terminal", row));
    }

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    for (int i = 0; i < (int)G_N_ELEMENTS(keypad); i++) {
        GtkWidget *b = key_button(p, &keypad[i]);
        gtk_widget_set_size_request(b, 64, 52);
        gtk_grid_attach(GTK_GRID(grid), b, i % 3, i / 3, 1, 1);
    }
    lights = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    for (int i = 0; i < 8; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(dot, "h89-dot");
        gtk_widget_add_css_class(dot, "h89-dot-off");
        gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), dot);
        gtk_box_append(GTK_BOX(row), gtk_label_new(LIGHT_NAMES[i]));
        gtk_box_append(GTK_BOX(lights), row);
        p->lights[i] = dot;
    }
    {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
        GtkWidget *kp = group("Keypad", grid);
        gtk_widget_set_hexpand(kp, TRUE);
        gtk_box_append(GTK_BOX(row), kp);
        gtk_box_append(GTK_BOX(row), group("Modes", lights));
        gtk_box_append(GTK_BOX(box), row);
    }

    header = adw_header_bar_new();
    toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), box);
    adw_window_set_content(ADW_WINDOW(p->win), toolbar);

    keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_panel_key), p);
    gtk_widget_add_controller(GTK_WIDGET(p->win), keys);

    g_signal_connect(p->win, "destroy", G_CALLBACK(on_destroy), p);
    p->timer = g_timeout_add(250, refresh_lights, p);
    refresh_lights(p);
    g_panel = p;
    gtk_window_present(p->win);
}
