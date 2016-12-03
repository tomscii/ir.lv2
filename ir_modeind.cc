/*
    Copyright (C) 2011 Tom Szilagyi

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>

#include "ir_modeind.h"
#include "ir_utils.h"

G_DEFINE_TYPE(IRModeInd, ir_modeind, GTK_TYPE_DRAWING_AREA);

typedef struct _IRModeIndPrivate IRModeIndPrivate;

#define IR_MODEIND_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IR_MODEIND_TYPE, IRModeIndPrivate))

struct _IRModeIndPrivate {
	int channels;
};


static gboolean ir_modeind_expose(GtkWidget * ir_modeind, GdkEventExpose * event);

static void ir_modeind_class_init(IRModeIndClass * cls) {
	GObjectClass * obj_class = G_OBJECT_CLASS(cls);
	GtkWidgetClass * widget_class = GTK_WIDGET_CLASS(cls);
	widget_class->expose_event = ir_modeind_expose;
	g_type_class_add_private(obj_class, sizeof(IRModeIndPrivate));
}

static void ir_modeind_init(IRModeInd * w) {
	IRModeIndPrivate * p = IR_MODEIND_GET_PRIVATE(w);
	p->channels = 0;
}

void draw_line(cairo_t * cr, int x1, int y1, int x2, int y2) {

	cairo_move_to(cr, x1, y1);
	cairo_line_to(cr, x2, y2);
}

static void draw(GtkWidget * widget, cairo_t * cr) {
	IRModeIndPrivate * p = IR_MODEIND_GET_PRIVATE(widget);
	int w = widget->allocation.width;
	int h = widget->allocation.height;
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke(cr);

	int nchan = p->channels;
	if (!nchan) {
		return;
	}

	const char * text = NULL;
	switch (nchan) {
	case 1:	text = "Mono";
		break;
	case 2: text = "Stereo";
		break;
	case 4: text = "True Stereo";
		break;
	}

	cairo_set_source_rgb(cr, 1, 1, 1);
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 10.0);

	draw_centered_text(cr, text, w/2, h*7/8);
	draw_centered_text(cr, "L", w * 2/16, h * 7/32);
	draw_centered_text(cr, "R", w * 2/16, h * 19/32);
	draw_centered_text(cr, "in", w * 2/16, h * 13/32);
	draw_centered_text(cr, "L", w * 14/16, h * 7/32);
	draw_centered_text(cr, "R", w * 14/16, h * 19/32);
	draw_centered_text(cr, "out", w * 14/16, h * 13/32);

	/* draw lines */
	switch (nchan) {
	case 1:
	case 2:
		draw_line(cr, w * 7/32, h * 7/32, w * 14/32, h * 7/32);
		draw_line(cr, w * 7/32, h * 19/32, w * 14/32, h * 19/32);
		draw_line(cr, w * 18/32, h * 7/32, w * 25/32, h * 7/32);
		draw_line(cr, w * 18/32, h * 19/32, w * 25/32, h * 19/32);
	break;
	case 4:
		draw_line(cr, w * 7/32, h * 7/32, w * 14/32, h * 2/16);
		draw_line(cr, w * 7/32, h * 7/32, w * 14/32, h * 5/16);
		draw_line(cr, w * 7/32, h * 19/32, w * 14/32, h * 8/16);
		draw_line(cr, w * 7/32, h * 19/32, w * 14/32, h * 11/16);

		draw_line(cr, w * 18/32, h * 2/16, w * 25/32, h * 7/32);
		draw_line(cr, w * 18/32, h * 8/16, w * 25/32, h * 7/32);
		draw_line(cr, w * 18/32, h * 5/16, w * 25/32, h * 19/32);
		draw_line(cr, w * 18/32, h * 11/16, w * 25/32, h * 19/32);
		break;
	}
	cairo_stroke(cr);

	/* draw two or four boxes atop each other */
	switch (nchan) {
	case 1:
		cairo_set_source_rgb(cr, 0, 0.25, 0.8);
		cairo_rectangle(cr, w * 7/16, h * 5/32, w/8, h/8);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);

		cairo_rectangle(cr, w * 7/16, h * 17/32, w/8, h/8);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0, 0, 0);
		draw_centered_text(cr, "1", w/2, h * 7/32);
		draw_centered_text(cr, "1", w/2, h * 19/32);
		cairo_stroke(cr);
		break;
	case 2:
		cairo_set_source_rgb(cr, 0, 0.25, 0.8);
		cairo_rectangle(cr, w * 7/16, h * 5/32, w/8, h/8);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0.25, 0.8, 0);
		cairo_rectangle(cr, w * 7/16, h * 17/32, w/8, h/8);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0, 0, 0);
		draw_centered_text(cr, "1", w/2, h * 7/32);
		draw_centered_text(cr, "2", w/2 - 1, h * 19/32);
		cairo_stroke(cr);
		break;
	case 4:
		cairo_set_source_rgb(cr, 0, 0.25, 0.8);
		cairo_rectangle(cr, w * 7/16, h * 1/16, w/8, h/8);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0.25, 0.8, 0);
		cairo_rectangle(cr, w * 7/16, h * 4/16, w/8, h/8);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0.8, 0.2, 0.4);
		cairo_rectangle(cr, w * 7/16, h * 7/16, w/8, h/8);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0.7, 0.7, 0.4);
		cairo_rectangle(cr, w * 7/16, h * 10/16, w/8, h/8);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0, 0, 0);
		draw_centered_text(cr, "1", w/2, h * 2/16);
		draw_centered_text(cr, "2", w/2 - 1, h * 5/16);
		draw_centered_text(cr, "3", w/2 - 1, h * 8/16);
		draw_centered_text(cr, "4", w/2 - 1, h * 11/16);
		cairo_stroke(cr);
		break;
	}
}

static gboolean ir_modeind_expose(GtkWidget * widget, GdkEventExpose * event) {
	cairo_t * cr = gdk_cairo_create(widget->window);
	cairo_rectangle(cr, event->area.x, event->area.y,
			event->area.width, event->area.height);
	cairo_clip(cr);
	draw(widget, cr);
	cairo_destroy(cr);
	return FALSE;
}

void ir_modeind_redraw(IRModeInd * w) {
	GtkWidget * widget;
	GdkRegion * region;
	widget = GTK_WIDGET(w);
	if (!widget->window) {
		return;
	}
	region = gdk_drawable_get_clip_region(widget->window);
	gdk_window_invalidate_region(widget->window, region, TRUE);
	gdk_window_process_updates(widget->window, TRUE);
	gdk_region_destroy(region);
}

void ir_modeind_set_channels(IRModeInd * w, int channels) {
	if (!w || !GTK_IS_WIDGET(w)) {
		return;
	}
	IRModeIndPrivate * p = IR_MODEIND_GET_PRIVATE(w);
	p->channels = channels;
	ir_modeind_redraw(w);
}

GtkWidget * ir_modeind_new(void) {
	return (GtkWidget *)g_object_new(IR_MODEIND_TYPE, NULL);
}
