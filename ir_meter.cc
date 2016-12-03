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

#include "ir_meter.h"
#include "ir_utils.h"

G_DEFINE_TYPE(IRMeter, ir_meter, GTK_TYPE_DRAWING_AREA);

typedef struct _IRMeterPrivate IRMeterPrivate;

#define IR_METER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IR_METER_TYPE, IRMeterPrivate))

struct _IRMeterPrivate {
	GdkPixmap * pixmap;
	float level;
};


static gboolean ir_meter_expose(GtkWidget * widget, GdkEventExpose * event);
static gboolean ir_meter_configure(GtkWidget * widget, GdkEventConfigure * event);
static void ir_meter_destroy(GtkObject * object);

static void ir_meter_class_init(IRMeterClass * cls) {
	GtkObjectClass * obj_class = GTK_OBJECT_CLASS(cls); /* changed G_ to GTK_ */
	GtkWidgetClass * widget_class = GTK_WIDGET_CLASS(cls);
	widget_class->expose_event = ir_meter_expose;
	widget_class->configure_event = ir_meter_configure;
	obj_class->destroy = ir_meter_destroy;
	g_type_class_add_private(obj_class, sizeof(IRMeterPrivate));
}

static void ir_meter_init(IRMeter * w) {
	IRMeterPrivate * p = IR_METER_GET_PRIVATE(w);
	p->pixmap = NULL;
	p->level = 0.0;
}

static void ir_meter_destroy(GtkObject * object) {
	IRMeter * w = IR_METER(object);
	IRMeterPrivate * p = IR_METER_GET_PRIVATE(w);
	if (p->pixmap) {
		gdk_pixmap_unref(p->pixmap);
		p->pixmap = NULL;
	}
}

static void draw_fullscale(GtkWidget * widget) {
	IRMeterPrivate * p = IR_METER_GET_PRIVATE(widget);
	int w = widget->allocation.width;
	int h = widget->allocation.height;

	cairo_t * cr = gdk_cairo_create(p->pixmap);

	float fzero = 1.0f + 90.0f/96.0f; /* result of convert_real_to_scale(ADJ_*_GAIN, 0) */
	fzero = exp10(fzero);
	fzero = (fzero - 10.0f) / 90.0f;
	int zero = h * (1.0 - fzero);

	cairo_rectangle(cr, 0, 0, w, zero);
	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 1, 0, 0);
	cairo_stroke(cr);

	int n = h - zero - 1;
	int i;
	for (i = 0; i < n/2; i++) {
		cairo_set_source_rgb(cr, 0, 1.0, 2.0*(float)i/n);
		cairo_move_to(cr, 0, zero + i + 1);
		cairo_line_to(cr, w, zero + i + 1);
		cairo_stroke(cr);
	}
	for (; i < n; i++) {
		cairo_set_source_rgb(cr, 0, 1.0 - 2.0*(float)(i-n/2)/n, 1.0);
		cairo_move_to(cr, 0, zero + i + 1);
		cairo_line_to(cr, w, zero + i + 1);
		cairo_stroke(cr);
	}
	cairo_destroy(cr);
}

static void draw(GtkWidget * widget) {
	IRMeterPrivate * p = IR_METER_GET_PRIVATE(widget);
	int w = widget->allocation.width;
	int h = widget->allocation.height;
	int pos = h * (1.0f - (p->level - 10.0f) / 90.0f);

	cairo_t * cr = gdk_cairo_create(widget->window);
	gdk_cairo_set_source_pixmap(cr, p->pixmap, 0, 0);

	cairo_rectangle(cr, 0, 0, w, pos);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke(cr);

	cairo_destroy(cr);
}

static gboolean ir_meter_configure(GtkWidget * widget, GdkEventConfigure * event) {
	IRMeterPrivate * p = IR_METER_GET_PRIVATE(widget);
	int w = widget->allocation.width;
	int h = widget->allocation.height;
	if (p->pixmap) {
		gdk_pixmap_unref(p->pixmap);
	}
	p->pixmap = gdk_pixmap_new(widget->window, w, h, -1);
	draw_fullscale(widget);
	draw(widget);
	return TRUE;
}

static gboolean ir_meter_expose(GtkWidget * widget, GdkEventExpose * event) {
	IRMeterPrivate * p = IR_METER_GET_PRIVATE(widget);
	gdk_draw_drawable(widget->window,
			  widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
			  p->pixmap,
			  event->area.x, event->area.y,
			  event->area.x, event->area.y,
			  event->area.width, event->area.height);
	draw(widget);
	return FALSE;
}

void ir_meter_redraw(IRMeter * w) {
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

void ir_meter_redraw_all(IRMeter * w) {
	GtkWidget * widget = GTK_WIDGET(w);
	if (!widget->window) {
		return;
	}
	draw_fullscale(widget);
	ir_meter_redraw(w);
}

void ir_meter_set_level(IRMeter * w, float level) {
	if (!w || !GTK_IS_WIDGET(w)) {
		return;
	}
	IRMeterPrivate * p = IR_METER_GET_PRIVATE(w);
	p->level = level;
	ir_meter_redraw(w);
}

GtkWidget * ir_meter_new(void) {
	return (GtkWidget *)g_object_new(IR_METER_TYPE, NULL);
}
