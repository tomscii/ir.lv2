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

#include "ir_wavedisplay.h"
#include "ir_utils.h"

G_DEFINE_TYPE(IRWaveDisplay, ir_wavedisplay, GTK_TYPE_DRAWING_AREA);

typedef struct _IRWaveDisplayPrivate IRWaveDisplayPrivate;

#define IR_WAVEDISPLAY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), IR_WAVEDISPLAY_TYPE, IRWaveDisplayPrivate))

struct _IRWaveDisplayPrivate {
	GdkPixmap * pixmap;
	const char * msg;
	float progress;
	float * wave; /* owned by widget (copy of original data) */
	int wave_length;
	int logarithmic;
	int attack_time_s;
	float attack_pc;
	float env_pc;
	float length_pc;
	int reverse;
};


static gboolean ir_wavedisplay_expose(GtkWidget * widget, GdkEventExpose * event);
static gboolean ir_wavedisplay_configure(GtkWidget * widget, GdkEventConfigure * event);
static void ir_wavedisplay_destroy(GtkObject * object);

static void ir_wavedisplay_class_init(IRWaveDisplayClass * cls) {
	GtkObjectClass * obj_class = GTK_OBJECT_CLASS(cls); /* changed G_ to GTK_ */
	GtkWidgetClass * widget_class = GTK_WIDGET_CLASS(cls);
	widget_class->expose_event = ir_wavedisplay_expose;
	widget_class->configure_event = ir_wavedisplay_configure;
	obj_class->destroy = ir_wavedisplay_destroy;
	g_type_class_add_private(obj_class, sizeof(IRWaveDisplayPrivate));
}

static void ir_wavedisplay_init(IRWaveDisplay * w) {
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(w);
	p->pixmap = NULL;
	p->msg = NULL;
	p->progress = -1.0;
	p->wave = NULL;
	p->wave_length = 0;
	p->logarithmic = 0;
	p->attack_time_s = 0;
	p->attack_pc = 0.0f;
	p->env_pc = 0.0f;
	p->length_pc = 0.0f;
}

static void ir_wavedisplay_destroy(GtkObject * object) {
	IRWaveDisplay * w = IR_WAVEDISPLAY(object);
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(w);
	if (p->pixmap) {
		gdk_pixmap_unref(p->pixmap);
		p->pixmap = NULL;
	}
	if (p->wave) {
		free(p->wave);
		p->wave = NULL;
	}
}

float y_transform(float value, int logarithmic) {
	float ret = value;
	if (logarithmic) {
		ret = 20.0*log10f(ret);
		if (ret < DISPLAY_DB_MIN) {
			ret = DISPLAY_DB_MIN;
		}
		ret /= DISPLAY_DB_MIN;
	} else {
		ret = 1 - ret;
	}
	return ret;
}

static void draw_wave(GtkWidget * widget) {
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(widget);
	int w = widget->allocation.width;
	int h = widget->allocation.height;

	cairo_t * cr = gdk_cairo_create(p->pixmap);

	cairo_rectangle(cr, 0, 0, w, h);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_fill_preserve(cr);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_stroke(cr);

	if (!p->wave || !p->wave_length) {
		cairo_destroy(cr);
		return;
	}

	int logarithmic = p->logarithmic;

	/* draw IR waveform */
	float samples_per_pixel = (float)p->wave_length / w;
	float * f = p->wave;
	for (int i = 0; i < w; i++) {
		int index_0 = i * samples_per_pixel;
		int index_1 = (i+1) * samples_per_pixel;
		float rms = 0.0f, max = 0.0f, v, a;
		for (int j = index_0; j < index_1; j++) {
			v = *f++;
			rms += v*v;
			if ((a = fabs(v)) > max) {
				max = a;
			}
		}
		rms = sqrt(rms / samples_per_pixel);

		max = y_transform(max, logarithmic);
		rms = y_transform(rms, logarithmic);

		cairo_set_source_rgb(cr, 0, 0.25, 0.8);
		cairo_move_to(cr, i, h);
		cairo_line_to(cr, i, h * max);
		cairo_stroke(cr);

		cairo_set_source_rgb(cr, 0, 0.2, 0.6);
		cairo_move_to(cr, i, h);
		cairo_line_to(cr, i, h * rms);
		cairo_stroke(cr);
	}
	cairo_destroy(cr);
}

static void draw_envelope(GtkWidget * widget) {
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(widget);
	int w = widget->allocation.width;
	int h = widget->allocation.height;

	cairo_t * cr = gdk_cairo_create(widget->window);
	gdk_cairo_set_source_pixmap(cr, p->pixmap, 0, 0);

	int logarithmic = p->logarithmic;

	if (p->wave && p->wave_length) {
		/* draw envelope */
		cairo_set_source_rgb(cr, 1, 0.375, 0);
		float * envelope = (float *)malloc(w * sizeof(float));
		for (int i = 0; i < w; i++) {
			envelope[i] = 1.0f;
		}
		int attack_time_s = p->attack_time_s * w / p->wave_length;
		compute_envelope(&envelope, 1, w, attack_time_s, p->attack_pc,
				 p->env_pc, p->length_pc);
		if (p->reverse) {
			float tmp;
			for (int i = 0, j = w-1; i < w/2; i++, j--) {
				tmp = envelope[i];
				envelope[i] = envelope[j];
				envelope[j] = tmp;
			}
		}
		float y = y_transform(envelope[0], logarithmic);
		for (int i = 0; i < w; i++) {
			float y_new = y_transform(envelope[i], logarithmic);
			cairo_move_to(cr, i-1, h * y);
			cairo_line_to(cr, i, h * y_new);
			cairo_stroke(cr);
			y = y_new;
		}
		free(envelope);
	}

	/* draw progress bar */
	if (p->progress >= 0.0) {
		cairo_rectangle(cr, 1, h - 10, (w-1) * p->progress, 9);
		cairo_set_source_rgba(cr, 1, 0.2, 0.2, 0.6);
		cairo_fill_preserve(cr);
		cairo_stroke(cr);
	}

	/* draw message */
	if (p->msg) {
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
				       CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 20.0);
		draw_centered_text(cr, p->msg, w/2, h/2);
		cairo_stroke(cr);
	}
	cairo_destroy(cr);
}

static gboolean ir_wavedisplay_configure(GtkWidget * widget, GdkEventConfigure * event) {
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(widget);
	int w = widget->allocation.width;
	int h = widget->allocation.height;
	if (p->pixmap) {
		gdk_pixmap_unref(p->pixmap);
	}
	p->pixmap = gdk_pixmap_new(widget->window, w, h, -1);
	draw_wave(widget);
	gdk_draw_drawable(widget->window,
			  widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
			  p->pixmap, 0, 0, 0, 0, w, h);
	return TRUE;
}

static gboolean ir_wavedisplay_expose(GtkWidget * widget, GdkEventExpose * event) {
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(widget);
	gdk_draw_drawable(widget->window,
			  widget->style->fg_gc[GTK_WIDGET_STATE(widget)],
			  p->pixmap,
			  event->area.x, event->area.y,
			  event->area.x, event->area.y,
			  event->area.width, event->area.height);
	draw_envelope(widget);
	return FALSE;
}

void ir_wavedisplay_redraw(IRWaveDisplay * w) {
	if (!w || !GTK_IS_WIDGET(w)) {
		return;
	}
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

void ir_wavedisplay_redraw_all(IRWaveDisplay * w) {
	if (!w || !GTK_IS_WIDGET(w)) {
		return;
	}
	GtkWidget * widget = GTK_WIDGET(w);
	if (!widget->window) {
		return;
	}
	draw_wave(widget);
	ir_wavedisplay_redraw(w);
}

void ir_wavedisplay_set_message(IRWaveDisplay * w, const char * msg) {
	if (!w || !GTK_IS_WIDGET(w)) {
		return;
	}
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(w);
	p->msg = msg;
	ir_wavedisplay_redraw(w);
}

void ir_wavedisplay_set_progress(IRWaveDisplay * w, const float progress) {
	if (!w || !GTK_IS_WIDGET(w)) {
		return;
	}
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(w);
	if (p->progress != progress) {
		p->progress = progress;
		ir_wavedisplay_redraw(w);
	}
}

void ir_wavedisplay_set_wave(IRWaveDisplay * w, float * values, int length) {
	if (!w || !GTK_IS_WIDGET(w) || !values || !length) {
		return;
	}
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(w);
	p->msg = NULL;
	if (p->wave) {
		free(p->wave);
	}
	p->wave = (float*)malloc(length * sizeof(float));
	p->wave_length = length;
	for (int i = 0; i < length; i++) {
		p->wave[i] = values[i];
	}
	ir_wavedisplay_redraw_all(w);
}

void ir_wavedisplay_set_logarithmic(IRWaveDisplay * w, int yes) {
	if (!w || !GTK_IS_WIDGET(w)) {
		return;
	}
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(w);
	int y = (yes) ? 1 : 0;
	if (p->logarithmic != y) {
		p->logarithmic = y;
		ir_wavedisplay_redraw_all(w);
	}
}

void ir_wavedisplay_set_envparams(IRWaveDisplay * w,
				  int attack_time_s, float attack_pc,
				  float env_pc, float length_pc, int reverse) {
	if (!w || !GTK_IS_WIDGET(w)) {
		return;
	}
	IRWaveDisplayPrivate * p = IR_WAVEDISPLAY_GET_PRIVATE(w);
	p->attack_time_s = attack_time_s;
	p->attack_pc = attack_pc;
	p->env_pc = env_pc;
	p->length_pc = length_pc;
	p->reverse = reverse;
	ir_wavedisplay_redraw(w);
}

GtkWidget * ir_wavedisplay_new(void) {
	return (GtkWidget *)g_object_new(IR_WAVEDISPLAY_TYPE, NULL);
}
