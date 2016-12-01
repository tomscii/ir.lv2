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

#ifndef _IR_METER_H
#define _IR_METER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define IR_METER_TYPE		(ir_meter_get_type())
#define IR_METER(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), IR_METER_TYPE, IRMeter))
#define IR_METER_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST((obj), IR_METER, IRMeterClass))
#define IR_METER_GET_CLASS	(G_TYPE_INSTANCE_GET_CLASS ((obj), IR_METER_TYPE, IRMeterClass))

typedef struct _IRMeter		IRMeter;
typedef struct _IRMeterClass	IRMeterClass;

struct _IRMeter {
	GtkDrawingArea parent;
	/* < private > */
};

struct _IRMeterClass {
	GtkDrawingAreaClass parent_class;
	static void (*destroy)(GtkObject * object);
};

GType ir_meter_get_type();

GtkWidget * ir_meter_new(void);
void ir_meter_redraw(IRMeter * w);
void ir_meter_redraw_all(IRMeter * w);
void ir_meter_set_level(IRMeter * w, float level);

G_END_DECLS

#endif /* _IR_METER_H */
