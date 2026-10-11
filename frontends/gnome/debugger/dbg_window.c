/*
 * Debugger window (GTK4/libadwaita) over the session's debugger engine,
 * via core/include/h89debug.h.
 *
 * Tabs: a Prompt (the engine's command set, with Tab completion and a
 * command history), the Z80 and memory, the disassembly with breakpoints a
 * click away, breakpoints, and the H89 (the terminal, the memory map, the
 * timer and interrupts, the disk controllers, the serial ports and the
 * FujiNet card, as the engine reports them). A toolbar carries the stepping
 * controls, the radix (hex, octal, or Heath's split octal), symbol loading
 * and saves; the family's keys apply (F5 run/stop, F7 step, F8 step over,
 * Shift+F8 step out, F12 close).
 *
 * The engine engages only while this window shows: showing it attaches
 * (which stops the machine, as on every sibling), hiding it detaches and
 * lets the machine run on. The window polls the engine's generation
 * counter and refreshes when something changed, and twice a second while
 * the machine runs.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dbg_window.h"

#include <stdlib.h>
#include <string.h>

#include "h89debug.h"
#include "../window.h"

#define DISASM_WINDOW 48
#define NREGS 17
#define MEM_ROWS 32

typedef struct {
    GtkWindow *win;
    h89session *session;
    h89debug *dbg;
    unsigned seen_generation;
    gboolean was_stopped;
    int running_ticks;
    guint timer;

    AdwWindowTitle *title;
    GtkButton *run_btn;
    AdwToastOverlay *toasts;
    GtkNotebook *notebook;
    GtkDropDown *radix;

    GtkTextView *prompt_out;
    GtkEntry *prompt_in;
    GtkScrolledWindow *prompt_scroll;
    GPtrArray *history;
    guint history_pos;

    GtkEntry *reg[NREGS];
    GtkCheckButton *flag[6];
    GtkLabel *cpu_info;
    GtkTextView *mem_view;
    GtkEntry *mem_goto, *mem_addr, *mem_val;
    int mem_base;
    gboolean updating_flags;

    GtkTextView *disasm;
    GtkCheckButton *follow_pc;
    GtkEntry *jump;
    int disasm_top;
    uint16_t line_addr[DISASM_WINDOW];
    int line_count;

    GtkListBox *bp_list;
    GtkDropDown *bp_type;
    GtkEntry *bp_start, *bp_end, *bp_cond;
    GtkLabel *bp_msg;

    GtkTextView *h89_text;
} DbgWin;

static DbgWin *g_win;

static const struct { const char *name; int reg; int wide; } k_regs[NREGS] = {
    { "PC", H89_REG_PC, 1 }, { "SP", H89_REG_SP, 1 }, { "AF", H89_REG_AF, 1 },
    { "BC", H89_REG_BC, 1 }, { "DE", H89_REG_DE, 1 }, { "HL", H89_REG_HL, 1 },
    { "IX", H89_REG_IX, 1 }, { "IY", H89_REG_IY, 1 },
    { "AF'", H89_REG_AF2, 1 }, { "BC'", H89_REG_BC2, 1 }, { "DE'", H89_REG_DE2, 1 },
    { "HL'", H89_REG_HL2, 1 }, { "I", H89_REG_I, 0 }, { "R", H89_REG_R, 0 },
    { "IM", H89_REG_IM, -1 }, { "IFF1", H89_REG_IFF1, -1 }, { "IFF2", H89_REG_IFF2, -1 },
};

/* The tabs, in order, by the names H89_DEBUGGER_TAB takes. */
static const char *const k_tab_names[] = { "prompt", "cpu", "disassembly", "breakpoints", "h89" };

/* ---- helpers --------------------------------------------------------------------- */

/* Replaces a view's text where the reader left it. */
static void set_text(GtkTextView *view, const char *text)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(view);
    GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(view));
    GtkAdjustment *adj = GTK_IS_SCROLLED_WINDOW(parent)
        ? gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent)) : NULL;
    GtkTextIter it;
    int top = 0;

    if (adj && gtk_adjustment_get_value(adj) > 0.0) {
        gtk_text_view_get_line_at_y(view, &it, (int)gtk_adjustment_get_value(adj), NULL);
        top = gtk_text_iter_get_line(&it);
    }
    gtk_text_buffer_set_text(b, text, -1);
    if (top > 0) {
        GtkTextMark *mark;
        gtk_text_buffer_get_iter_at_line(b, &it, top);
        mark = gtk_text_buffer_create_mark(b, NULL, &it, TRUE);
        gtk_text_view_scroll_to_mark(view, mark, 0.0, TRUE, 0.0, 0.0);
        gtk_text_buffer_delete_mark(b, mark);
    }
}

static void append_text(GtkTextView *view, GtkScrolledWindow *scroll, const char *text)
{
    GtkTextBuffer *b = gtk_text_view_get_buffer(view);
    GtkTextIter end;
    GtkAdjustment *adj;
    gtk_text_buffer_get_end_iter(b, &end);
    gtk_text_buffer_insert(b, &end, text, -1);
    adj = gtk_scrolled_window_get_vadjustment(scroll);
    if (adj)
        gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
}

/* An expression as typed into a box (any radix, labels, registers). */
static int parse_expr(DbgWin *w, const char *text, long *out)
{
    char err[96];
    while (*text == ' ')
        text++;
    if (!*text)
        return 0;
    return h89debug_eval(w->dbg, text, out, err, sizeof err) == 0;
}

/* The live refresh does not overwrite a value being typed. */
static void set_entry(DbgWin *w, GtkEntry *e, const char *text)
{
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(e));
    GtkWidget *focus = root ? gtk_root_get_focus(root) : NULL;
    if (focus && !h89debug_is_stopped(w->dbg) &&
        (focus == GTK_WIDGET(e) || gtk_widget_is_ancestor(focus, GTK_WIDGET(e))))
        return;
    gtk_editable_set_text(GTK_EDITABLE(e), text);
}

static GtkWidget *mono_view(GtkTextView **out, gboolean editable)
{
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), editable);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), editable);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 6);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_hexpand(scroll, TRUE);
    *out = GTK_TEXT_VIEW(view);
    return scroll;
}

static GtkWidget *labeled(const char *text, GtkWidget *child)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *l = gtk_label_new(text);
    gtk_widget_add_css_class(l, "dim-label");
    gtk_box_append(GTK_BOX(box), l);
    gtk_box_append(GTK_BOX(box), child);
    return box;
}

static GtkWidget *padded(GtkWidget *w)
{
    gtk_widget_set_margin_top(w, 8);
    gtk_widget_set_margin_bottom(w, 8);
    gtk_widget_set_margin_start(w, 8);
    gtk_widget_set_margin_end(w, 8);
    return w;
}

static void accent_tag(GtkTextView *view, const char *name)
{
    char rgb[16];
    g_snprintf(rgb, sizeof rgb, "#%06x", H89SESSION_ACCENT_RGB);
    gtk_text_buffer_create_tag(gtk_text_view_get_buffer(view), name, "paragraph-background", rgb,
                               "foreground", "#000000", NULL);
}

static void prompt_say(DbgWin *w, const char *msg)
{
    char line[700];
    g_snprintf(line, sizeof line, "%s\n", msg);
    append_text(w->prompt_out, w->prompt_scroll, line);
    adw_toast_overlay_add_toast(w->toasts, adw_toast_new(msg));
}

/* ---- refresh ------------------------------------------------------------------------ */

static void refresh_cpu(DbgWin *w)
{
    h89debug_cpu c;
    char buf[200];
    h89debug_cpu_get(w->dbg, &c);
    {
        const int vals[NREGS] = { c.pc, c.sp, c.af, c.bc, c.de, c.hl, c.ix, c.iy, c.af2, c.bc2,
                                  c.de2, c.hl2, c.i, c.r, c.im, c.iff1, c.iff2 };
        for (int i = 0; i < NREGS; i++) {
            if (k_regs[i].wide > 0)
                h89debug_fmt16(w->dbg, (unsigned)vals[i], buf, sizeof buf);
            else if (k_regs[i].wide == 0)
                h89debug_fmt8(w->dbg, (unsigned)vals[i], buf, sizeof buf);
            else
                g_snprintf(buf, sizeof buf, "%d", vals[i]);
            set_entry(w, w->reg[i], buf);
        }
    }
    {
        const int flags[6] = { c.sf, c.zf, c.hf, c.pf, c.nf, c.cf };
        w->updating_flags = TRUE;
        for (int i = 0; i < 6; i++)
            gtk_check_button_set_active(w->flag[i], flags[i] != 0);
        w->updating_flags = FALSE;
    }
    g_snprintf(buf, sizeof buf, "%llu cycles   %llu ticks of the 2 ms clock%s",
               (unsigned long long)c.cycles, (unsigned long long)c.ticks,
               c.halted ? "   HALT" : "");
    gtk_label_set_text(w->cpu_info, buf);
}

static void refresh_mem(DbgWin *w)
{
    static char text[MEM_ROWS * 128 + 64];
    uint8_t row[16];
    const int per = h89debug_radix(w->dbg) == H89_RADIX_HEX ? 16 : 8;
    int len = 0;
    for (int r = 0; r < MEM_ROWS; r++) {
        uint16_t a = (uint16_t)(w->mem_base + r * per);
        char at[16], v[8];
        h89debug_read(w->dbg, a, row, per);
        len += g_snprintf(text + len, sizeof text - len, "%-8s ",
                          h89debug_fmt16(w->dbg, a, at, sizeof at));
        for (int i = 0; i < per; i++)
            len += g_snprintf(text + len, sizeof text - len, "%s ",
                              h89debug_fmt8(w->dbg, row[i], v, sizeof v));
        len += g_snprintf(text + len, sizeof text - len, " ");
        for (int i = 0; i < per; i++)
            len += g_snprintf(text + len, sizeof text - len, "%c",
                              row[i] >= 0x20 && row[i] < 0x7f ? row[i] : '.');
        len += g_snprintf(text + len, sizeof text - len, "\n");
    }
    set_text(w->mem_view, text);
}

static void refresh_disasm(DbgWin *w)
{
    static h89debug_line lines[DISASM_WINDOW];
    static char text[DISASM_WINDOW * 220];
    int n, pc_line, len = 0;
    GtkTextBuffer *b = gtk_text_view_get_buffer(w->disasm);
    GtkTextIter s, e;

    /* following, the PC sits a third of the way down */
    if (gtk_check_button_get_active(w->follow_pc)) {
        h89debug_cpu c;
        h89debug_cpu_get(w->dbg, &c);
        w->disasm_top = h89debug_row_address(w->dbg, (uint16_t)c.pc, -(DISASM_WINDOW / 3));
    }
    n = h89debug_disassemble(w->dbg, (uint16_t)w->disasm_top, lines, DISASM_WINDOW, &pc_line);
    w->line_count = n;
    for (int i = 0; i < n; i++) {
        w->line_addr[i] = lines[i].address;
        len += g_snprintf(text + len, sizeof text - len, "%c%c %-8s %-16s %-14s %s%s%s\n",
                          lines[i].has_breakpoint ? '*' : ' ', lines[i].is_pc ? '>' : ' ',
                          lines[i].addr_text, lines[i].bytes, lines[i].label, lines[i].disasm,
                          lines[i].comment[0] ? "  ; " : "", lines[i].comment);
    }
    if (n == 0)
        len += g_snprintf(text + len, sizeof text - len, "(no disassembly)\n");
    gtk_text_buffer_set_text(b, text, -1);
    if (pc_line >= 0 && pc_line < n) {
        gtk_text_buffer_get_iter_at_line(b, &s, pc_line);
        gtk_text_buffer_get_iter_at_line(b, &e, pc_line + 1);
        gtk_text_buffer_apply_tag_by_name(b, "pc", &s, &e);
    }
}

static void refresh_h89(DbgWin *w)
{
    static char text[16384], part[4096];
    int len = 0;
    for (int s = 0; s < H89DEBUG_SECTION_COUNT; s++) {
        h89debug_section(w->dbg, s, part, sizeof part);
        len += g_snprintf(text + len, sizeof text - len, "%s%s\n%s", s ? "\n" : "",
                          h89debug_section_name(s), part);
    }
    set_text(w->h89_text, text);
}

static void bp_enable_toggled(GtkCheckButton *b, gpointer ud);
static void bp_remove_clicked(GtkButton *b, gpointer ud);

static const char *bp_type_name(int type)
{
    if (type & H89DEBUG_BP_EXEC) return "Execute";
    if ((type & (H89DEBUG_BP_READ | H89DEBUG_BP_WRITE)) == (H89DEBUG_BP_READ | H89DEBUG_BP_WRITE))
        return "Read/Write";
    if (type & H89DEBUG_BP_READ) return "Read";
    if (type & H89DEBUG_BP_WRITE) return "Write";
    if (type & H89DEBUG_BP_IN) return "Port In";
    return "Port Out";
}

static void refresh_bps(DbgWin *w)
{
    h89debug_breakpoint bps[128];
    GtkWidget *child;
    int n = h89debug_breakpoint_list(w->dbg, bps, 128);

    while ((child = gtk_widget_get_first_child(GTK_WIDGET(w->bp_list))) != NULL)
        gtk_list_box_remove(w->bp_list, child);
    if (n == 0) {
        GtkWidget *l = gtk_label_new("No breakpoints. Click a disassembly line, or add one below.");
        gtk_widget_add_css_class(l, "dim-label");
        gtk_list_box_append(w->bp_list, l);
        return;
    }
    for (int i = 0; i < n; i++) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *en = gtk_check_button_new();
        GtkWidget *rm = gtk_button_new_from_icon_name("user-trash-symbolic");
        GtkWidget *l;
        char text[320], range[40], a[16], b[16];
        const gboolean port = (bps[i].type & (H89DEBUG_BP_IN | H89DEBUG_BP_OUT)) != 0;
        if (port) {
            h89debug_fmt8(w->dbg, bps[i].start, a, sizeof a);
            h89debug_fmt8(w->dbg, bps[i].end, b, sizeof b);
        } else {
            h89debug_fmt16(w->dbg, bps[i].start, a, sizeof a);
            h89debug_fmt16(w->dbg, bps[i].end, b, sizeof b);
        }
        if (bps[i].end != bps[i].start)
            g_snprintf(range, sizeof range, "%s-%s", a, b);
        else
            g_snprintf(range, sizeof range, "%s", a);
        g_snprintf(text, sizeof text, "#%-3d %-10s %-16s hits %-6u %s%s", bps[i].id,
                   bp_type_name(bps[i].type), range, bps[i].hits,
                   bps[i].condition[0] ? "if " : "", bps[i].condition);
        l = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(l), 0);
        gtk_widget_set_hexpand(l, TRUE);
        gtk_widget_add_css_class(l, "monospace");
        gtk_check_button_set_active(GTK_CHECK_BUTTON(en), bps[i].enabled != 0);
        gtk_widget_set_tooltip_text(en, "Enabled");
        g_object_set_data(G_OBJECT(en), "bp", GINT_TO_POINTER(bps[i].id));
        g_object_set_data(G_OBJECT(rm), "bp", GINT_TO_POINTER(bps[i].id));
        g_signal_connect(en, "toggled", G_CALLBACK(bp_enable_toggled), w);
        g_signal_connect(rm, "clicked", G_CALLBACK(bp_remove_clicked), w);
        gtk_widget_add_css_class(rm, "flat");
        gtk_widget_set_tooltip_text(rm, "Remove");
        gtk_box_append(GTK_BOX(row), en);
        gtk_box_append(GTK_BOX(row), l);
        gtk_box_append(GTK_BOX(row), rm);
        gtk_list_box_append(w->bp_list, row);
    }
}

static void refresh_status(DbgWin *w)
{
    char reason[160], text[256];
    int addr;
    gboolean stopped = h89debug_is_stopped(w->dbg) != 0;
    h89debug_stop_reason(w->dbg, reason, sizeof reason, &addr);
    if (stopped)
        g_snprintf(text, sizeof text, "Stopped%s%s", reason[0] ? ": " : "", reason);
    else
        g_snprintf(text, sizeof text, "%s", h89debug_is_attached(w->dbg) ? "Running" : "The machine is off");
    adw_window_title_set_subtitle(w->title, text);
    gtk_button_set_label(w->run_btn, stopped ? "Run (F5)" : "Stop (F5)");
    if (stopped)
        gtk_widget_add_css_class(GTK_WIDGET(w->run_btn), "h89-accent");
    else
        gtk_widget_remove_css_class(GTK_WIDGET(w->run_btn), "h89-accent");
}

static void refresh_all(DbgWin *w)
{
    refresh_status(w);
    refresh_cpu(w);
    refresh_mem(w);
    refresh_disasm(w);
    refresh_bps(w);
    refresh_h89(w);
}

static void attach_and_refresh(DbgWin *w);

static gboolean tick_refresh(gpointer ud)
{
    DbgWin *w = ud;
    unsigned gen;
    gboolean stopped;
    if (!gtk_widget_get_visible(GTK_WIDGET(w->win)))
        return G_SOURCE_CONTINUE;
    /* opened before the machine started, or across a restart: the engine
     * comes up with the machine */
    if (!h89debug_is_attached(w->dbg) && h89session_is_running(w->session)) {
        attach_and_refresh(w);
        return G_SOURCE_CONTINUE;
    }
    gen = h89debug_generation(w->dbg);
    stopped = h89debug_is_stopped(w->dbg) != 0;
    if (gen != w->seen_generation || stopped != w->was_stopped) {
        w->seen_generation = gen;
        w->was_stopped = stopped;
        refresh_all(w);
    } else if (!stopped && ++w->running_ticks >= 5) {
        /* live values twice a second while the machine runs */
        w->running_ticks = 0;
        refresh_status(w);
        refresh_cpu(w);
        refresh_mem(w);
        refresh_h89(w);
    }
    return G_SOURCE_CONTINUE;
}

/* ---- handlers ----------------------------------------------------------------------- */

static void on_run(GtkButton *b, gpointer ud)
{
    DbgWin *w = ud;
    (void)b;
    if (h89debug_is_stopped(w->dbg))
        h89debug_resume(w->dbg);
    else
        h89debug_stop(w->dbg);
    refresh_all(w);
}

static void on_step(GtkButton *b, gpointer ud) { (void)b; h89debug_step(((DbgWin *)ud)->dbg); refresh_all(ud); }
static void on_step_over(GtkButton *b, gpointer ud) { (void)b; h89debug_step_over(((DbgWin *)ud)->dbg); refresh_all(ud); }
static void on_step_out(GtkButton *b, gpointer ud) { (void)b; h89debug_step_out(((DbgWin *)ud)->dbg); refresh_all(ud); }
static void on_tick(GtkButton *b, gpointer ud) { (void)b; h89debug_ticks(((DbgWin *)ud)->dbg, 1); refresh_all(ud); }

static void on_radix(GObject *o, GParamSpec *ps, gpointer ud)
{
    DbgWin *w = ud;
    int r = (int)gtk_drop_down_get_selected(GTK_DROP_DOWN(o));
    (void)ps;
    h89debug_set_radix(w->dbg, r);
    h89session_set_int(w->session, "debugger.radix", r);
    refresh_all(w);
}

static void on_prompt(GtkEntry *entry, gpointer ud)
{
    DbgWin *w = ud;
    static char out[65536];
    const char *cmd = gtk_editable_get_text(GTK_EDITABLE(entry));
    char line[512];
    if (!cmd || !*cmd)
        return;
    if (w->history->len == 0 ||
        strcmp(g_ptr_array_index(w->history, w->history->len - 1), cmd) != 0)
        g_ptr_array_add(w->history, g_strdup(cmd));
    w->history_pos = w->history->len;
    g_snprintf(line, sizeof line, "> %s\n", cmd);
    append_text(w->prompt_out, w->prompt_scroll, line);
    h89debug_command(w->dbg, cmd, out, sizeof out);
    append_text(w->prompt_out, w->prompt_scroll, out);
    if (out[0] && out[strlen(out) - 1] != '\n')
        append_text(w->prompt_out, w->prompt_scroll, "\n");
    gtk_editable_set_text(GTK_EDITABLE(entry), "");
    /* "radix" from the prompt moves the toolbar's too */
    if ((int)gtk_drop_down_get_selected(w->radix) != h89debug_radix(w->dbg))
        gtk_drop_down_set_selected(w->radix, (guint)h89debug_radix(w->dbg));
    refresh_all(w);
}

static void history_show(DbgWin *w)
{
    const char *text = w->history_pos < w->history->len
                           ? g_ptr_array_index(w->history, w->history_pos) : "";
    gtk_editable_set_text(GTK_EDITABLE(w->prompt_in), text);
    gtk_editable_set_position(GTK_EDITABLE(w->prompt_in), -1);
}

/* Tab completes the word against the commands, registers and labels;
 * several matches are listed. Up and Down walk the history. */
static gboolean on_prompt_key(GtkEventControllerKey *c, guint keyval, guint code,
                              GdkModifierType st, gpointer ud)
{
    DbgWin *w = ud;
    char comps[4096];
    const char *text, *word;
    int n;
    (void)c; (void)code; (void)st;
    if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up) {
        if (w->history_pos > 0) {
            w->history_pos--;
            history_show(w);
        }
        return TRUE;
    }
    if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) {
        if (w->history_pos < w->history->len) {
            w->history_pos++;
            history_show(w);
        }
        return TRUE;
    }
    if (keyval != GDK_KEY_Tab)
        return FALSE;
    text = gtk_editable_get_text(GTK_EDITABLE(w->prompt_in));
    word = strrchr(text, ' ');
    word = word ? word + 1 : text;
    n = h89debug_completions(w->dbg, text, comps, sizeof comps);
    if (n == 1) {
        char merged[600];
        char *nl = strchr(comps, '\n');
        if (nl)
            *nl = '\0';
        g_snprintf(merged, sizeof merged, "%.*s%s ", (int)(word - text), text, comps);
        gtk_editable_set_text(GTK_EDITABLE(w->prompt_in), merged);
        gtk_editable_set_position(GTK_EDITABLE(w->prompt_in), -1);
    } else if (n > 1) {
        append_text(w->prompt_out, w->prompt_scroll, comps);
        if (comps[0] && comps[strlen(comps) - 1] != '\n')
            append_text(w->prompt_out, w->prompt_scroll, "\n");
    }
    return TRUE;
}

static void on_reg_activate(GtkEntry *entry, gpointer ud)
{
    DbgWin *w = ud;
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(entry), "reg"));
    long v;
    if (parse_expr(w, gtk_editable_get_text(GTK_EDITABLE(entry)), &v))
        h89debug_cpu_set(w->dbg, k_regs[i].reg, (int)v);
    refresh_all(w);
}

static void on_flag_toggled(GtkCheckButton *b, gpointer ud)
{
    DbgWin *w = ud;
    int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "flag"));
    static const int flags[6] = { H89_FLAG_S, H89_FLAG_Z, H89_FLAG_H, H89_FLAG_PV, H89_FLAG_N,
                                  H89_FLAG_C };
    if (w->updating_flags || !h89debug_is_stopped(w->dbg))
        return;
    h89debug_cpu_set(w->dbg, flags[i], gtk_check_button_get_active(b));
}

static void on_mem_goto(GtkEntry *entry, gpointer ud)
{
    DbgWin *w = ud;
    long a;
    if (parse_expr(w, gtk_editable_get_text(GTK_EDITABLE(entry)), &a))
        w->mem_base = (int)(a & 0xfff0);
    refresh_mem(w);
}

static void on_mem_write(GtkEntry *entry, gpointer ud)
{
    DbgWin *w = ud;
    long a, v;
    (void)entry;
    if (!parse_expr(w, gtk_editable_get_text(GTK_EDITABLE(w->mem_addr)), &a) ||
        !parse_expr(w, gtk_editable_get_text(GTK_EDITABLE(w->mem_val)), &v)) {
        adw_toast_overlay_add_toast(w->toasts, adw_toast_new("An address and a value, please"));
        return;
    }
    h89debug_write(w->dbg, (uint16_t)a, (uint8_t)v);
    refresh_all(w);
}

static gboolean on_mem_scroll(GtkEventControllerScroll *c, double dx, double dy, gpointer ud)
{
    DbgWin *w = ud;
    const int per = h89debug_radix(w->dbg) == H89_RADIX_HEX ? 16 : 8;
    (void)c; (void)dx;
    w->mem_base = (w->mem_base + (dy > 0 ? 4 : -4) * per) & 0xffff;
    refresh_mem(w);
    return TRUE;
}

static void on_disasm_click(GtkGestureClick *g, int n, double x, double y, gpointer ud)
{
    DbgWin *w = ud;
    GtkTextIter it;
    int bx, by, line;
    (void)g; (void)n;
    gtk_text_view_window_to_buffer_coords(w->disasm, GTK_TEXT_WINDOW_WIDGET, (int)x, (int)y, &bx, &by);
    gtk_text_view_get_iter_at_location(w->disasm, &it, bx, by);
    line = gtk_text_iter_get_line(&it);
    if (line < 0 || line >= w->line_count)
        return;
    h89debug_breakpoint_toggle(w->dbg, w->line_addr[line]);
    refresh_disasm(w);
    refresh_bps(w);
}

static void on_follow_toggled(GtkCheckButton *b, gpointer ud)
{
    (void)b;
    refresh_disasm(ud);
}

static void on_jump(GtkEntry *entry, gpointer ud)
{
    DbgWin *w = ud;
    long a;
    if (!parse_expr(w, gtk_editable_get_text(GTK_EDITABLE(entry)), &a))
        return;
    gtk_check_button_set_active(w->follow_pc, FALSE);
    w->disasm_top = (int)(a & 0xffff);
    refresh_disasm(w);
}

static gboolean on_disasm_scroll(GtkEventControllerScroll *c, double dx, double dy, gpointer ud)
{
    DbgWin *w = ud;
    int rows = (int)(dy * 3);
    (void)c; (void)dx;
    if (!rows)
        rows = dy > 0 ? 1 : -1;
    gtk_check_button_set_active(w->follow_pc, FALSE);
    w->disasm_top = h89debug_row_address(w->dbg, (uint16_t)w->disasm_top, rows);
    refresh_disasm(w);
    return TRUE;
}

static void bp_enable_toggled(GtkCheckButton *b, gpointer ud)
{
    DbgWin *w = ud;
    h89debug_breakpoint_enable(w->dbg, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "bp")),
                               gtk_check_button_get_active(b));
    refresh_disasm(w);
}

static void bp_remove_clicked(GtkButton *b, gpointer ud)
{
    DbgWin *w = ud;
    h89debug_breakpoint_remove(w->dbg, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "bp")));
    refresh_all(w);
}

static void on_bp_add(GtkWidget *wgt, gpointer ud)
{
    DbgWin *w = ud;
    static const int types[6] = { H89DEBUG_BP_EXEC, H89DEBUG_BP_READ, H89DEBUG_BP_WRITE,
                                  H89DEBUG_BP_READ | H89DEBUG_BP_WRITE, H89DEBUG_BP_IN,
                                  H89DEBUG_BP_OUT };
    guint sel = gtk_drop_down_get_selected(w->bp_type);
    const char *end = gtk_editable_get_text(GTK_EDITABLE(w->bp_end));
    long a, b;
    int id;
    (void)wgt;
    if (sel >= G_N_ELEMENTS(types))
        sel = 0;
    if (!parse_expr(w, gtk_editable_get_text(GTK_EDITABLE(w->bp_start)), &a)) {
        gtk_label_set_text(w->bp_msg, "Not an address, a label or an expression");
        return;
    }
    b = a;
    if (end && *end && !parse_expr(w, end, &b)) {
        gtk_label_set_text(w->bp_msg, "The end is not an address, a label or an expression");
        return;
    }
    id = h89debug_breakpoint_add(w->dbg, types[sel], (uint16_t)a, (uint16_t)b,
                                 gtk_editable_get_text(GTK_EDITABLE(w->bp_cond)));
    if (id < 0) {
        gtk_label_set_text(w->bp_msg, "The condition does not parse");
        return;
    }
    gtk_label_set_text(w->bp_msg, "");
    gtk_editable_set_text(GTK_EDITABLE(w->bp_start), "");
    gtk_editable_set_text(GTK_EDITABLE(w->bp_end), "");
    gtk_editable_set_text(GTK_EDITABLE(w->bp_cond), "");
    refresh_all(w);
}

static void on_bp_clear(GtkButton *b, gpointer ud)
{
    DbgWin *w = ud;
    (void)b;
    h89debug_breakpoint_clear(w->dbg);
    refresh_all(w);
}

static void on_symbols_chosen(GObject *src, GAsyncResult *res, gpointer ud)
{
    DbgWin *w = ud;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(src), res, NULL);
    g_autofree char *path = file ? g_file_get_path(file) : NULL;
    char msg[512];
    if (!path)
        return;
    h89debug_load_symbols(w->dbg, path, msg, sizeof msg);
    prompt_say(w, msg);
    refresh_all(w);
}

static void on_symbols(GtkButton *b, gpointer ud)
{
    DbgWin *w = ud;
    GtkFileDialog *dlg = gtk_file_dialog_new();
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *syms = gtk_file_filter_new();
    GtkFileFilter *all = gtk_file_filter_new();
    (void)b;
    gtk_file_filter_set_name(syms, "Symbol files (*.map, *.sym, *.noi, *.txt)");
    gtk_file_filter_add_suffix(syms, "map");
    gtk_file_filter_add_suffix(syms, "sym");
    gtk_file_filter_add_suffix(syms, "noi");
    gtk_file_filter_add_suffix(syms, "txt");
    gtk_file_filter_set_name(all, "All files");
    gtk_file_filter_add_pattern(all, "*");
    g_list_store_append(filters, syms);
    g_list_store_append(filters, all);
    gtk_file_dialog_set_title(dlg, "Load Symbols (z88dk .map, .sym, SDCC .noi, AAAA name)");
    gtk_file_dialog_set_filters(dlg, G_LIST_MODEL(filters));
    gtk_file_dialog_open(dlg, w->win, NULL, on_symbols_chosen, w);
    g_object_unref(syms);
    g_object_unref(all);
    g_object_unref(filters);
    g_object_unref(dlg);
}

static void on_save_chosen(GObject *src, GAsyncResult *res, gpointer ud)
{
    DbgWin *w = ud;
    g_autoptr(GFile) file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(src), res, NULL);
    g_autofree char *path = file ? g_file_get_path(file) : NULL;
    const char *kind = g_object_get_data(src, "kind");
    char msg[512];
    if (!path)
        return;
    h89debug_save(w->dbg, kind, path, msg, sizeof msg);
    prompt_say(w, msg);
}

static void on_save(GtkButton *b, gpointer ud)
{
    DbgWin *w = ud;
    GtkFileDialog *dlg = gtk_file_dialog_new();
    GtkWidget *pop = gtk_widget_get_ancestor(GTK_WIDGET(b), GTK_TYPE_POPOVER);
    char title[64];
    if (pop)
        gtk_popover_popdown(GTK_POPOVER(pop));
    g_snprintf(title, sizeof title, "Save %s", (const char *)g_object_get_data(G_OBJECT(b), "title"));
    gtk_file_dialog_set_title(dlg, title);
    gtk_file_dialog_set_initial_name(dlg, g_object_get_data(G_OBJECT(b), "file"));
    g_object_set_data(G_OBJECT(dlg), "kind", g_object_get_data(G_OBJECT(b), "kind"));
    gtk_file_dialog_save(dlg, w->win, NULL, on_save_chosen, w);
    g_object_unref(dlg);
}

static void hide_window(DbgWin *w)
{
    h89debug_detach(w->dbg);
    gtk_widget_set_visible(GTK_WIDGET(w->win), FALSE);
}

static gboolean on_key(GtkEventControllerKey *c, guint keyval, guint code, GdkModifierType st,
                       gpointer ud)
{
    DbgWin *w = ud;
    (void)c; (void)code;
    switch (keyval) {
    case GDK_KEY_F5: on_run(NULL, w); return TRUE;
    case GDK_KEY_F7: on_step(NULL, w); return TRUE;
    case GDK_KEY_F8:
        if (st & GDK_SHIFT_MASK)
            on_step_out(NULL, w);
        else
            on_step_over(NULL, w);
        return TRUE;
    case GDK_KEY_F12: hide_window(w); return TRUE;
    default: return FALSE;
    }
}

static gboolean on_close(GtkWindow *win, gpointer ud)
{
    (void)win;
    hide_window(ud);
    return TRUE;
}

/* ---- construction ----------------------------------------------------------------------- */

static GtkWidget *tool_button(const char *label, const char *tip, GCallback cb, DbgWin *w)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_set_tooltip_text(b, tip);
    g_signal_connect(b, "clicked", cb, w);
    return b;
}

static GtkWidget *build_toolbar(DbgWin *w)
{
    static const struct { const char *kind, *title, *file; } saves[] = {
        { "dis", "Disassembly (the 64K as mapped)", "disassembly.asm" },
        { "ram", "Memory (the 64K as the CPU sees it)", "memory.bin" },
        { "regs", "Registers and the H89's state", "h89-state.txt" },
    };
    static const char *const radixes[] = { "Hex", "Octal", "Split octal", NULL };
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *save, *pop, *popbox;

    w->run_btn = GTK_BUTTON(tool_button("Stop (F5)", "Run or stop", G_CALLBACK(on_run), w));
    gtk_box_append(GTK_BOX(bar), GTK_WIDGET(w->run_btn));
    gtk_box_append(GTK_BOX(bar), tool_button("Step (F7)", "One instruction", G_CALLBACK(on_step), w));
    gtk_box_append(GTK_BOX(bar), tool_button("Over (F8)", "Over a CALL, RST, DJNZ or block op",
                                             G_CALLBACK(on_step_over), w));
    gtk_box_append(GTK_BOX(bar), tool_button("Out (⇧F8)", "To the return", G_CALLBACK(on_step_out), w));
    gtk_box_append(GTK_BOX(bar), tool_button("Tick", "One 2 ms clock tick", G_CALLBACK(on_tick), w));

    w->radix = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(radixes));
    gtk_widget_set_tooltip_text(GTK_WIDGET(w->radix),
                                "How numbers are shown: hex, octal, or Heath's split octal (041.000)");
    gtk_drop_down_set_selected(w->radix, (guint)h89debug_radix(w->dbg));
    g_signal_connect(w->radix, "notify::selected", G_CALLBACK(on_radix), w);
    gtk_box_append(GTK_BOX(bar), GTK_WIDGET(w->radix));

    {
        GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(GTK_BOX(bar), spacer);
    }
    gtk_box_append(GTK_BOX(bar), tool_button("Symbols…", "Load a symbol file", G_CALLBACK(on_symbols), w));
    save = gtk_menu_button_new();
    gtk_menu_button_set_label(GTK_MENU_BUTTON(save), "Save");
    pop = gtk_popover_new();
    popbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    for (unsigned i = 0; i < G_N_ELEMENTS(saves); i++) {
        GtkWidget *b = gtk_button_new_with_label(saves[i].title);
        gtk_widget_add_css_class(b, "flat");
        g_object_set_data(G_OBJECT(b), "kind", (gpointer)saves[i].kind);
        g_object_set_data(G_OBJECT(b), "title", (gpointer)saves[i].title);
        g_object_set_data(G_OBJECT(b), "file", (gpointer)saves[i].file);
        g_signal_connect(b, "clicked", G_CALLBACK(on_save), w);
        gtk_box_append(GTK_BOX(popbox), b);
    }
    gtk_popover_set_child(GTK_POPOVER(pop), popbox);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(save), pop);
    gtk_box_append(GTK_BOX(bar), save);
    return padded(bar);
}

static GtkWidget *build_prompt(DbgWin *w)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *scroll = mono_view(&w->prompt_out, FALSE);
    GtkEventController *keys = gtk_event_controller_key_new();
    w->prompt_scroll = GTK_SCROLLED_WINDOW(scroll);
    w->prompt_in = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(w->prompt_in, "help, step, break 040.000, mem hl, radix split, fdc ...");
    gtk_widget_add_css_class(GTK_WIDGET(w->prompt_in), "monospace");
    g_signal_connect(w->prompt_in, "activate", G_CALLBACK(on_prompt), w);
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_prompt_key), w);
    gtk_widget_add_controller(GTK_WIDGET(w->prompt_in), keys);
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(w->prompt_in));
    append_text(w->prompt_out, w->prompt_scroll, "FujiNet Go H89 debugger. Type help for the commands.\n");
    return padded(box);
}

static GtkWidget *build_cpu(DbgWin *w)
{
    static const char *const flag_names[6] = { "S", "Z", "H", "P/V", "N", "C" };
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *flags = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *memrow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *scroll;
    GtkEventController *sc;

    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    for (int i = 0; i < NREGS; i++) {
        GtkWidget *l = gtk_label_new(k_regs[i].name);
        GtkWidget *e = gtk_entry_new();
        gtk_label_set_xalign(GTK_LABEL(l), 1);
        gtk_editable_set_width_chars(GTK_EDITABLE(e), 9);
        gtk_widget_add_css_class(e, "monospace");
        g_object_set_data(G_OBJECT(e), "reg", GINT_TO_POINTER(i));
        g_signal_connect(e, "activate", G_CALLBACK(on_reg_activate), w);
        w->reg[i] = GTK_ENTRY(e);
        gtk_grid_attach(GTK_GRID(grid), l, (i % 6) * 2, i / 6, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), e, (i % 6) * 2 + 1, i / 6, 1, 1);
    }
    gtk_box_append(GTK_BOX(box), grid);
    for (int i = 0; i < 6; i++) {
        GtkWidget *c = gtk_check_button_new_with_label(flag_names[i]);
        g_object_set_data(G_OBJECT(c), "flag", GINT_TO_POINTER(i));
        g_signal_connect(c, "toggled", G_CALLBACK(on_flag_toggled), w);
        w->flag[i] = GTK_CHECK_BUTTON(c);
        gtk_box_append(GTK_BOX(flags), c);
    }
    gtk_box_append(GTK_BOX(box), labeled("Flags", flags));
    w->cpu_info = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(w->cpu_info, 0);
    gtk_widget_add_css_class(GTK_WIDGET(w->cpu_info), "monospace");
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(w->cpu_info));

    w->mem_goto = GTK_ENTRY(gtk_entry_new());
    w->mem_addr = GTK_ENTRY(gtk_entry_new());
    w->mem_val = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(w->mem_goto, "address, label or expression");
    gtk_editable_set_width_chars(GTK_EDITABLE(w->mem_addr), 10);
    gtk_editable_set_width_chars(GTK_EDITABLE(w->mem_val), 6);
    g_signal_connect(w->mem_goto, "activate", G_CALLBACK(on_mem_goto), w);
    g_signal_connect(w->mem_val, "activate", G_CALLBACK(on_mem_write), w);
    gtk_box_append(GTK_BOX(memrow), labeled("Memory at", GTK_WIDGET(w->mem_goto)));
    gtk_box_append(GTK_BOX(memrow), labeled("Write", GTK_WIDGET(w->mem_addr)));
    gtk_box_append(GTK_BOX(memrow), labeled("=", GTK_WIDGET(w->mem_val)));
    gtk_box_append(GTK_BOX(box), memrow);
    scroll = mono_view(&w->mem_view, FALSE);
    sc = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(sc, "scroll", G_CALLBACK(on_mem_scroll), w);
    gtk_widget_add_controller(GTK_WIDGET(w->mem_view), sc);
    gtk_box_append(GTK_BOX(box), scroll);
    return padded(box);
}

static GtkWidget *build_disasm(DbgWin *w)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *scroll = mono_view(&w->disasm, FALSE);
    GtkGesture *click = gtk_gesture_click_new();
    GtkEventController *sc = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    GtkWidget *hint;

    w->follow_pc = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Follow PC"));
    gtk_check_button_set_active(w->follow_pc, TRUE);
    g_signal_connect(w->follow_pc, "toggled", G_CALLBACK(on_follow_toggled), w);
    w->jump = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(w->jump, "go to an address or a label");
    g_signal_connect(w->jump, "activate", G_CALLBACK(on_jump), w);
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(w->follow_pc));
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(w->jump));
    hint = gtk_label_new("Click a line to toggle its breakpoint; scroll to browse");
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_box_append(GTK_BOX(row), hint);
    accent_tag(w->disasm, "pc");
    g_signal_connect(click, "pressed", G_CALLBACK(on_disasm_click), w);
    gtk_widget_add_controller(GTK_WIDGET(w->disasm), GTK_EVENT_CONTROLLER(click));
    g_signal_connect(sc, "scroll", G_CALLBACK(on_disasm_scroll), w);
    gtk_widget_add_controller(GTK_WIDGET(w->disasm), sc);
    gtk_box_append(GTK_BOX(box), row);
    gtk_box_append(GTK_BOX(box), scroll);
    return padded(box);
}

static GtkWidget *build_bps(DbgWin *w)
{
    static const char *const types[] = { "Execute", "Read", "Write", "Read/Write", "Port In",
                                         "Port Out", NULL };
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *scroll = gtk_scrolled_window_new();
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *add = gtk_button_new_with_label("Add");
    GtkWidget *clear = gtk_button_new_with_label("Clear All");

    w->bp_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(w->bp_list, GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(w->bp_list));
    gtk_widget_set_vexpand(scroll, TRUE);
    w->bp_type = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(types));
    w->bp_start = GTK_ENTRY(gtk_entry_new());
    w->bp_end = GTK_ENTRY(gtk_entry_new());
    w->bp_cond = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(w->bp_start, "start");
    gtk_entry_set_placeholder_text(w->bp_end, "end (optional)");
    gtk_entry_set_placeholder_text(w->bp_cond, "condition, e.g. a == 0 or value == 3");
    gtk_widget_set_hexpand(GTK_WIDGET(w->bp_cond), TRUE);
    g_signal_connect(add, "clicked", G_CALLBACK(on_bp_add), w);
    g_signal_connect(w->bp_cond, "activate", G_CALLBACK(on_bp_add), w);
    g_signal_connect(clear, "clicked", G_CALLBACK(on_bp_clear), w);
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(w->bp_type));
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(w->bp_start));
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(w->bp_end));
    gtk_box_append(GTK_BOX(row), GTK_WIDGET(w->bp_cond));
    gtk_box_append(GTK_BOX(row), add);
    gtk_box_append(GTK_BOX(row), clear);
    w->bp_msg = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_xalign(w->bp_msg, 0);
    gtk_widget_add_css_class(GTK_WIDGET(w->bp_msg), "error");
    gtk_box_append(GTK_BOX(box), scroll);
    gtk_box_append(GTK_BOX(box), row);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(w->bp_msg));
    return padded(box);
}

static void attach_and_refresh(DbgWin *w)
{
    h89debug_attach(w->dbg);     /* stops the machine */
    w->seen_generation = h89debug_generation(w->dbg) - 1;
    w->was_stopped = !h89debug_is_stopped(w->dbg);
    refresh_all(w);
}

static void on_destroy(GtkWidget *wgt, gpointer ud)
{
    DbgWin *w = ud;
    (void)wgt;
    if (w->timer)
        g_source_remove(w->timer);
    h89debug_detach(w->dbg);
    g_ptr_array_free(w->history, TRUE);
    g_free(w);
    g_win = NULL;
}

/* The Prompt tab is for typing: its input has the keyboard whenever the tab
 * shows (otherwise the toolbar's first button would). */
static void focus_prompt_if_shown(DbgWin *w)
{
    if (gtk_notebook_get_current_page(w->notebook) == 0)
        gtk_widget_grab_focus(GTK_WIDGET(w->prompt_in));
}

static void on_switch_page(GtkNotebook *nb, GtkWidget *page, guint num, gpointer ud)
{
    (void)nb;
    (void)page;
    (void)num;
    focus_prompt_if_shown(ud);
}

void h89_debugger_show(GtkWindow *parent, h89session *session)
{
    DbgWin *w;
    GtkWidget *header, *toolbar, *box;
    GtkEventController *keys;

    if (g_win) {
        gtk_window_present(g_win->win);
        focus_prompt_if_shown(g_win);
        attach_and_refresh(g_win);
        return;
    }
    w = g_new0(DbgWin, 1);
    w->session = session;
    w->dbg = h89session_debugger(session);
    w->history = g_ptr_array_new_with_free_func(g_free);
    w->mem_base = 0x2000;
    w->win = GTK_WINDOW(adw_window_new());
    gtk_window_set_title(w->win, "Debugger");
    gtk_window_set_default_size(w->win, 980, 720);
    gtk_window_set_transient_for(w->win, parent);
    gtk_window_set_application(w->win, gtk_window_get_application(parent));
    g_signal_connect(w->win, "close-request", G_CALLBACK(on_close), w);
    g_signal_connect(w->win, "destroy", G_CALLBACK(on_destroy), w);

    w->title = ADW_WINDOW_TITLE(adw_window_title_new("Debugger", ""));
    header = adw_header_bar_new();
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header), GTK_WIDGET(w->title));

    w->notebook = GTK_NOTEBOOK(gtk_notebook_new());
    gtk_notebook_append_page(w->notebook, build_prompt(w), gtk_label_new("Prompt"));
    gtk_notebook_append_page(w->notebook, build_cpu(w), gtk_label_new("CPU & Memory"));
    gtk_notebook_append_page(w->notebook, build_disasm(w), gtk_label_new("Disassembly"));
    gtk_notebook_append_page(w->notebook, build_bps(w), gtk_label_new("Breakpoints"));
    {
        GtkWidget *scroll = mono_view(&w->h89_text, FALSE);
        gtk_notebook_append_page(w->notebook, padded(scroll), gtk_label_new("H89"));
    }
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), build_toolbar(w));
    gtk_widget_set_vexpand(GTK_WIDGET(w->notebook), TRUE);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(w->notebook));
    w->toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    adw_toast_overlay_set_child(w->toasts, box);
    toolbar = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(w->toasts));
    adw_window_set_content(ADW_WINDOW(w->win), toolbar);

    keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key), w);
    gtk_widget_add_controller(GTK_WIDGET(w->win), keys);

    /* H89_DEBUGGER_TAB=prompt|cpu|disassembly|breakpoints|h89 */
    {
        const char *tab = g_getenv("H89_DEBUGGER_TAB");
        for (unsigned i = 0; tab && i < G_N_ELEMENTS(k_tab_names); i++)
            if (!g_ascii_strcasecmp(tab, k_tab_names[i]))
                gtk_notebook_set_current_page(w->notebook, (int)i);
    }

    g_signal_connect_after(w->notebook, "switch-page", G_CALLBACK(on_switch_page), w);

    g_win = w;
    w->timer = g_timeout_add(100, tick_refresh, w);
    gtk_window_present(w->win);
    focus_prompt_if_shown(w);
    if (h89session_is_running(session))
        attach_and_refresh(w);
    else
        refresh_all(w);
}

void h89_debugger_toggle(GtkWindow *parent, h89session *session)
{
    if (g_win && gtk_widget_get_visible(GTK_WIDGET(g_win->win))) {
        hide_window(g_win);
        return;
    }
    h89_debugger_show(parent, session);
}
