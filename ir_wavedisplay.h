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

#ifndef _IR_WAVEDISPLAY_H
#define _IR_WAVEDISPLAY_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define IR_WAVEDISPLAY_TYPE		(ir_wavedisplay_get_type())
#define IR_WAVEDISPLAY(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), IR_WAVEDISPLAY_TYPE, IRWaveDisplay))
#define IR_WAVEDISPLAY_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST((obj), IR_WAVEDISPLAY, IRWaveDisplayClass))
#define IR_WAVEDISPLAY_GET_CLASS	(G_TYPE_INSTANCE_GET_CLASS ((obj), IR_WAVEDISPLAY_TYPE, IRWavedisplayClass))

typedef struct _IRWaveDisplay		IRWaveDisplay;
typedef struct _IRWaveDisplayClass	IRWaveDisplayClass;

struct _IRWaveDisplay {
	GtkDrawingArea parent;
	/* < private > */
};

struct _IRWaveDisplayClass {
	GtkDrawingAreaClass parent_class;
	static void (*destroy)(GtkObject * object);
};

GType ir_wavedisplay_get_type();

#define DISPLAY_DB_MIN -120.0f

GtkWidget * ir_wavedisplay_new(void);
void ir_wavedisplay_redraw(IRWaveDisplay * w);
void ir_wavedisplay_redraw_all(IRWaveDisplay * w);
void ir_wavedisplay_set_message(IRWaveDisplay * w, const char * msg);
void ir_wavedisplay_set_progress(IRWaveDisplay * w, const float progress);
void ir_wavedisplay_set_wave(IRWaveDisplay * w, float * values, int length);
void ir_wavedisplay_set_logarithmic(IRWaveDisplay * w, int yes);
void ir_wavedisplay_set_envparams(IRWaveDisplay * w,
				  int attack_time_s, float attack_pc,
				  float env_pc, float length_pc, int reverse);

G_END_DECLS

#endif /* _IR_WAVEDISPLAY_H */
