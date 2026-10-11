/*
 * display.c -- see display.h.
 *
 * Frames are pulled by serial, not pushed: copy_frame does nothing when the
 * H19 has not drawn anything new, so a tick between changes costs a mutex
 * and no memcpy. The H89 paces itself on its own 2 ms slices; the tick only
 * decides when the picture is looked at.
 *
 * The picture is 640 x 500: the H19's 640 x 250 raster with each scanline
 * doubled (the renderer leaves the second line dark with scanlines on).
 * The H19's tube was 4:3, so TV aspect shows it 667 wide for every 500
 * high; square pixels show the buffer as it is.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "display.h"

#define FB_W H89SESSION_FB_WIDTH
#define FB_H H89SESSION_FB_HEIGHT

struct _H89Display {
    GtkWidget parent_instance;

    h89session *session;
    guint32 *fb;           /* XRGB8888 straight from the session */
    guint32 *bgra;         /* alpha forced opaque for GdkMemoryTexture */
    GdkTexture *texture;
    guint64 serial;
    guint tick_id;
    gboolean tv_aspect;
    gboolean smooth;
};

G_DEFINE_FINAL_TYPE(H89Display, h89_display, GTK_TYPE_WIDGET)

static void rebuild_texture(H89Display *self)
{
    GBytes *bytes;
    int i, n = FB_W * FB_H;

    /* 0x00RRGGBB read as B,G,R,A on a little-endian host: only the alpha
     * needs setting */
    for (i = 0; i < n; i++)
        self->bgra[i] = self->fb[i] | 0xFF000000u;

    bytes = g_bytes_new_static(self->bgra, (gsize)n * 4);
    g_clear_object(&self->texture);
    self->texture = gdk_memory_texture_new(FB_W, FB_H, GDK_MEMORY_B8G8R8A8, bytes,
                                           (gsize)FB_W * 4);
    g_bytes_unref(bytes);
}

static gboolean on_tick(GtkWidget *widget, GdkFrameClock *clock, gpointer user_data)
{
    H89Display *self = H89_DISPLAY(widget);
    (void)user_data;

    h89session_notify_vsync(self->session, gdk_frame_clock_get_frame_time(clock) * 1000);
    if (h89session_copy_frame(self->session, self->fb, &self->serial)) {
        rebuild_texture(self);
        gtk_widget_queue_draw(widget);
    }
    return G_SOURCE_CONTINUE;
}

static void h89_display_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    H89Display *self = H89_DISPLAY(widget);
    int w = gtk_widget_get_width(widget);
    int h = gtk_widget_get_height(widget);
    double want, sw, sh, x, y;
    graphene_rect_t rect;

    gtk_snapshot_append_color(snapshot, &(GdkRGBA){ 0, 0, 0, 1 },
                              &GRAPHENE_RECT_INIT(0, 0, w, h));
    if (!self->texture || w <= 0 || h <= 0)
        return;

    want = self->tv_aspect ? 4.0 / 3.0 : (double)FB_W / (double)FB_H;
    if ((double)w / (double)h > want) {
        sh = h;
        sw = sh * want;
    } else {
        sw = w;
        sh = sw / want;
    }
    x = (w - sw) / 2.0;
    y = (h - sh) / 2.0;

    graphene_rect_init(&rect, (float)x, (float)y, (float)sw, (float)sh);
    gtk_snapshot_append_scaled_texture(
        snapshot, self->texture,
        self->smooth ? GSK_SCALING_FILTER_LINEAR : GSK_SCALING_FILTER_NEAREST, &rect);
}

static void h89_display_dispose(GObject *object)
{
    H89Display *self = H89_DISPLAY(object);

    if (self->tick_id) {
        gtk_widget_remove_tick_callback(GTK_WIDGET(self), self->tick_id);
        self->tick_id = 0;
    }
    g_clear_object(&self->texture);
    g_clear_pointer(&self->fb, g_free);
    g_clear_pointer(&self->bgra, g_free);

    G_OBJECT_CLASS(h89_display_parent_class)->dispose(object);
}

static void h89_display_class_init(H89DisplayClass *klass)
{
    G_OBJECT_CLASS(klass)->dispose = h89_display_dispose;
    GTK_WIDGET_CLASS(klass)->snapshot = h89_display_snapshot;
}

static void h89_display_init(H89Display *self)
{
    self->fb = g_new0(guint32, FB_W * FB_H);
    self->bgra = g_new0(guint32, FB_W * FB_H);
    self->tv_aspect = TRUE;
    self->smooth = TRUE;
    gtk_widget_set_focusable(GTK_WIDGET(self), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self), TRUE);
}

GtkWidget *h89_display_new(h89session *session)
{
    H89Display *self = g_object_new(H89_TYPE_DISPLAY, NULL);
    self->session = session;
    self->tick_id = gtk_widget_add_tick_callback(GTK_WIDGET(self), on_tick, NULL, NULL);
    return GTK_WIDGET(self);
}

void h89_display_set_tv_aspect(H89Display *self, gboolean tv)
{
    self->tv_aspect = tv;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}

void h89_display_set_smooth(H89Display *self, gboolean smooth)
{
    self->smooth = smooth;
    gtk_widget_queue_draw(GTK_WIDGET(self));
}
