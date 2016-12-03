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

#ifndef _IR_MODEIND_H
#define _IR_MODEIND_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define IR_MODEIND_TYPE		(ir_modeind_get_type())
#define IR_MODEIND(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), IR_MODEIND_TYPE, IRModeInd))
#define IR_MODEIND_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST((obj), IR_MODEIND, IRModeIndClass))
#define IR_MODEIND_GET_CLASS	(G_TYPE_INSTANCE_GET_CLASS ((obj), IR_MODEIND_TYPE, IRWavedisplayClass))

typedef struct _IRModeInd	IRModeInd;
typedef struct _IRModeIndClass	IRModeIndClass;

struct _IRModeInd {
	GtkDrawingArea parent;
	/* < private > */
};

struct _IRModeIndClass {
	GtkDrawingAreaClass parent_class;
};

GType ir_modeind_get_type();

GtkWidget * ir_modeind_new(void);
void ir_modeind_redraw(IRModeInd * w);
void ir_modeind_set_channels(IRModeInd * w, int channels);

G_END_DECLS

#endif /* _IR_MODEIND_H */
