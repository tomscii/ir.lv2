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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <gtk/gtk.h>

#include <lv2.h>
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "lv2/lv2plug.in/ns/ext/instance-access/instance-access.h"

#include "ir.h"
#include "ir_utils.h"
#include "ir_meter.h"
#include "ir_modeind.h"
#include "ir_wavedisplay.h"

#define IR_UI_URI "http://tomszilagyi.github.io/plugins/lv2/ir/gui"

#define PAD 2

typedef struct {
	uint32_t port_index;
	float value;
} port_event_t;

#define ADJ_PREDELAY   0
#define ADJ_ATTACK     1
#define ADJ_ATTACKTIME 2
#define ADJ_ENVELOPE   3
#define ADJ_LENGTH     4
#define ADJ_STRETCH    5
#define ADJ_STEREO_IN  6
#define ADJ_STEREO_IR  7
#define ADJ_DRY_GAIN   8
#define ADJ_WET_GAIN   9
#define N_ADJUSTMENTS 10

typedef struct {
	int id;
	gdouble def;
	gdouble min;
	gdouble max;
	int log;
} adj_descr_t;

#define LIN    0
#define LOG    1
#define INVLOG 2

static adj_descr_t adj_descr_table[N_ADJUSTMENTS] = {
	{ADJ_PREDELAY,     0.0,   0.0, 2000.0, INVLOG},
	{ADJ_ATTACK,       0.0,   0.0,  100.0, LIN},
	{ADJ_ATTACKTIME,   0.0,   0.0,  300.0, LIN},
	{ADJ_ENVELOPE,   100.0,   0.0,  100.0, LIN},
	{ADJ_LENGTH,     100.0,   0.0,  100.0, LIN},
	{ADJ_STRETCH,    100.0,  50.0,  150.0, LIN},
	{ADJ_STEREO_IN,  100.0,   0.0,  150.0, LIN},
	{ADJ_STEREO_IR,  100.0,   0.0,  150.0, LIN},
	{ADJ_DRY_GAIN,     0.0, -90.0,    6.0, LOG},
	{ADJ_WET_GAIN,    -6.0, -90.0,    6.0, LOG},
};

struct control {
	LV2UI_Controller controller;
	LV2UI_Write_Function write_function;

	IR * ir;

	GSList * port_event_q;
	GtkWidget * vbox_top;
	GtkWidget * hbox_waitplugin;

	float predelay;
	float attack;
	float attacktime;
	float envelope;
	float length;
	float stretch;
	float stereo_ir;

	GtkAdjustment * adj_predelay;
	GtkAdjustment * adj_attack;
	GtkAdjustment * adj_attacktime;
	GtkAdjustment * adj_envelope;
	GtkAdjustment * adj_length;
	GtkAdjustment * adj_stretch;
	GtkAdjustment * adj_stereo_in;
	GtkAdjustment * adj_stereo_ir;
	GtkAdjustment * adj_dry_gain;
	GtkAdjustment * adj_wet_gain;

	GtkWidget * scale_predelay;
	GtkWidget * scale_attack;
	GtkWidget * scale_attacktime;
	GtkWidget * scale_envelope;
	GtkWidget * scale_length;
	GtkWidget * scale_stretch;
	GtkWidget * scale_stereo_in;
	GtkWidget * scale_stereo_ir;

	GtkWidget * label_predelay;
	GtkWidget * label_attack;
	GtkWidget * label_envelope;
	GtkWidget * label_length;
	GtkWidget * label_stretch;
	GtkWidget * label_stereo;
	GtkWidget * label_dry_gain;
	GtkWidget * label_wet_gain;

	GtkWidget * toggle_reverse;
	gulong toggle_reverse_cbid;
	GtkWidget * toggle_agc_sw;
	GtkWidget * toggle_dry_sw;
	GtkWidget * toggle_wet_sw;

	GtkWidget * meter_L_dry;
	GtkWidget * meter_R_dry;
	GtkWidget * meter_L_wet;
	GtkWidget * meter_R_wet;

	GtkWidget * chan_toggle[4];
	gulong chan_toggle_cbid[4];
	GtkWidget * log_toggle;
	gulong log_toggle_cbid;
	GtkWidget * wave_display;
	GtkWidget * wave_annot_label;
	int disp_chan;
	GtkWidget * mode_ind;

	GtkTreeModel * model_bookmarks; /* GtkTreeModelSortable on ir->store_bookmarks */
	GtkListStore * store_files;
	GtkWidget * tree_bookmarks;
	GtkWidget * tree_files;
	int bookmarks_realized;
	int files_realized;

	gulong files_sel_cbid;
	gulong bookmarks_sel_cbid;
	guint timeout_tag;
	guint gui_load_timeout_tag;
	guint reinit_timeout_tag;
	guint waitplugin_timeout_tag;

	int interrupt_threads;
	GThread * gui_load_thread;
	GThread * reinit_thread;

	int key_pressed;
};

void reset_values(struct control * cp);

/* adjustments with linear or logarithmic scale */
#define LOG_SCALE_MIN       1.0
#define LOG_SCALE_MAX       2.0
#define INVLOG_SCALE_MIN   10.0
#define INVLOG_SCALE_MAX  100.0

void adjustment_changed_cb(GtkAdjustment * adj, gpointer data);

int get_adj_index(struct control * cp, GtkAdjustment * adj) {

	if (adj == cp->adj_predelay) {
		return ADJ_PREDELAY;
	} else if (adj == cp->adj_attack) {
		return ADJ_ATTACK;
	} else if (adj == cp->adj_attacktime) {
		return ADJ_ATTACKTIME;
	} else if (adj == cp->adj_envelope) {
		return ADJ_ENVELOPE;
	} else if (adj == cp->adj_length) {
		return ADJ_LENGTH;
	} else if (adj == cp->adj_stretch) {
		return ADJ_STRETCH;
	} else if (adj == cp->adj_stereo_in) {
		return ADJ_STEREO_IN;
	} else if (adj == cp->adj_stereo_ir) {
		return ADJ_STEREO_IR;
	} else if (adj == cp->adj_dry_gain) {
		return ADJ_DRY_GAIN;
	} else if (adj == cp->adj_wet_gain) {
		return ADJ_WET_GAIN;
	}
	return -1;
}

double convert_scale_to_real(int idx, double scale) {
	int log = adj_descr_table[idx].log;
	double y;
	double min = adj_descr_table[idx].min;
	double max = adj_descr_table[idx].max;
	double real = 0.0;
	if (log == LIN) {
		real = scale;
	} else if (log == LOG) {
		y = log10(scale);
		real = min + (y - LOG_SCALE_MIN) / (LOG_SCALE_MAX - LOG_SCALE_MIN) * (max - min);
		real = round(10.0 * real) / 10.0; /* one decimal digit */
	} else if (log == INVLOG) {
		y = exp10(scale);
		real = min + (y - INVLOG_SCALE_MIN) / (INVLOG_SCALE_MAX - INVLOG_SCALE_MIN) * (max - min);
		real = round(10.0 * real) / 10.0; /* one decimal digit */
	}
	return real;
}

double convert_real_to_scale(int idx, double real) {
	int log = adj_descr_table[idx].log;
	double min = adj_descr_table[idx].min;
	double max = adj_descr_table[idx].max;
	double scale = 0.0;
	if (log == LIN) {
		scale = real;
	} else if (log == LOG) {
		scale = (real - min) / (max - min) *
			(LOG_SCALE_MAX - LOG_SCALE_MIN) + LOG_SCALE_MIN;
		scale = exp10(scale);
	} else if (log == INVLOG) {
		scale = (real - min) / (max - min) *
			(INVLOG_SCALE_MAX - INVLOG_SCALE_MIN) + INVLOG_SCALE_MIN;
		scale = log10(scale);
	}
	return scale;
}

double get_adjustment(struct control * cp, GtkAdjustment * adj) {
	double y = gtk_adjustment_get_value(adj);
	int idx = get_adj_index(cp, adj);
	return convert_scale_to_real(idx, y);
}

void set_adjustment(struct control * cp, GtkAdjustment * adj, double x) {
	int idx = get_adj_index(cp, adj);
	gtk_adjustment_set_value(adj, convert_real_to_scale(idx, x));
}

GtkAdjustment * create_adjustment(int idx, gpointer data) {
	GtkObject * adj = NULL;
	gdouble def = adj_descr_table[idx].def;
	gdouble min = adj_descr_table[idx].min;
	gdouble max = adj_descr_table[idx].max;
	int log = adj_descr_table[idx].log;
	if ((log == LOG) || (log == INVLOG)) {
		adj = gtk_adjustment_new(convert_real_to_scale(idx, def),
					 convert_real_to_scale(idx, min),
					 convert_real_to_scale(idx, max) + 1.0,
					 0.01, 1.0, 1.0);
	} else {
		adj = gtk_adjustment_new(def, min, max + 1.0, 0.1, 1.0, 1.0);
	}
	g_signal_connect(adj, "value_changed", G_CALLBACK(adjustment_changed_cb), data);
	return (GtkAdjustment *)adj;
}


void set_agc_label(struct control * cp) {
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cp->toggle_agc_sw))) {
		char str[32];
		snprintf(str, 32, "Autogain %+.1f dB", cp->ir->autogain_new);
		gtk_button_set_label(GTK_BUTTON(cp->toggle_agc_sw), str);
	} else {
		gtk_button_set_label(GTK_BUTTON(cp->toggle_agc_sw), "Autogain off");
	}
}

#define S1 "<span size=\"small\">"
#define S2 "</span>"
#define XS1 "<span size=\"x-small\">"
#define XS2 "</span>"
void set_label(struct control * cp, int idx) {

	char str[1024];
	GtkWidget * label = NULL;
	float v;

	switch (idx) {
	case ADJ_PREDELAY:
		label = cp->label_predelay;
		v = get_adjustment(cp, cp->adj_predelay);
		snprintf(str, 1024, S1 "<b>Predelay</b>" S2 "\n" XS1 "%0.1fms" XS2,
			 fabs(v)); /* kill the spurious negative zero */
		break;
	case ADJ_ATTACK:
	case ADJ_ATTACKTIME:
		label = cp->label_attack; /* spaces eliminate label-positioning problem */
		snprintf(str, 1024, S1 "<b>      Attack</b>" S2 "\n" XS1 "%0.0f%%  %0.0fms" XS2,
			 get_adjustment(cp, cp->adj_attack),
			 get_adjustment(cp, cp->adj_attacktime));
		break;
	case ADJ_ENVELOPE:
		label = cp->label_envelope;
		snprintf(str, 1024, S1 "<b>Envelope</b>" S2 "\n" XS1 "%0.1f%%" XS2,
			 get_adjustment(cp, cp->adj_envelope));
		break;
	case ADJ_LENGTH:
		label = cp->label_length;
		snprintf(str, 1024, S1 "<b>Length</b>" S2 "\n" XS1"%0.1f%%" XS2,
			 get_adjustment(cp, cp->adj_length));
		break;
	case ADJ_STRETCH:
		label = cp->label_stretch;
		snprintf(str, 1024, S1 "<b>Stretch</b>" S2 "\n" XS1 "%0.1f%%" XS2,
			 get_adjustment(cp, cp->adj_stretch));
		break;
	case ADJ_STEREO_IN:
	case ADJ_STEREO_IR:
		label = cp->label_stereo;
		snprintf(str, 1024, S1 "<b>Stereo in/IR</b>" S2 "\n" XS1 "%0.0f%% / %0.0f%%" XS2,
			 get_adjustment(cp, cp->adj_stereo_in),
			 get_adjustment(cp, cp->adj_stereo_ir));
		break;
	case ADJ_DRY_GAIN:
		label = cp->label_dry_gain;
		v = get_adjustment(cp, cp->adj_dry_gain);
		if (v > 0.0) {
			snprintf(str, 1024, S1 "%+0.1f dB" S2, v);
		} else if (v == 0.0) {
			snprintf(str, 1024, S1 "0.0 dB" S2);
		} else if (v > -90.0) {
			snprintf(str, 1024, S1 "%+0.1f dB" S2, v);
		} else {
			snprintf(str, 1024, S1 "mute" S2);
		}
		break;
	case ADJ_WET_GAIN:
		label = cp->label_wet_gain;
		v = get_adjustment(cp, cp->adj_wet_gain);
		if (v > 0.0) {
			snprintf(str, 1024, S1 "%+0.1f dB" S2, v);
		} else if (v == 0.0) {
			snprintf(str, 1024, S1 "0.0 dB" S2);
		} else if (v > -90.0) {
			snprintf(str, 1024, S1 "%+0.1f dB" S2, v);
		} else {
			snprintf(str, 1024, S1 "mute" S2);
		}
		break;
	}

	gtk_label_set_markup(GTK_LABEL(label), str);
}

void refresh_gui_on_load(struct control * cp) {
	char str[1024];
	const char * chn = (cp->ir->nchan > 1) ? "channels" : "channel";
	float secs = (float)cp->ir->source_nfram / cp->ir->source_samplerate;
	char * filename_esc = g_markup_escape_text(cp->ir->source_path, -1);
	if (cp->ir->source_samplerate == (unsigned int)cp->ir->sample_rate) {
		snprintf(str, 1024,
			 XS1 "<b>%s</b>" XS2 "\n"
			 S1 "%d %s, %d samples, %d Hz, %.3f seconds" S2,
			 filename_esc,
			 cp->ir->nchan, chn, cp->ir->source_nfram,
			 cp->ir->source_samplerate, secs);
	} else {
		snprintf(str, 1024,
			 XS1 "<b>%s</b>" XS2 "\n"
			 S1 "%d %s, %d samples, %d Hz (resampled to %d Hz), %.3f seconds" S2,
			 filename_esc,
			 cp->ir->nchan, chn, cp->ir->source_nfram,
			 cp->ir->source_samplerate, (int)cp->ir->sample_rate, secs);
	}
	free(filename_esc);
	gtk_label_set_markup(GTK_LABEL(cp->wave_annot_label), str);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cp->chan_toggle[0]), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cp->chan_toggle[0]), TRUE);
	gtk_widget_set_sensitive(cp->chan_toggle[0], cp->ir->nchan > 1);
	for (int i = 1; i < 4; i++) {
		gtk_widget_set_sensitive(cp->chan_toggle[i], i < cp->ir->nchan);
	}

	set_agc_label(cp);
	ir_modeind_set_channels(IR_MODEIND(cp->mode_ind), cp->ir->nchan);
}

gpointer gui_load_thread(gpointer data) {
	struct control * cp = (struct control*)data;
	int r = cp->ir->resample_init(cp->ir);
	if (r == 0) {
		while (r == 0) {
			r = cp->ir->resample_do(cp->ir);
			if (cp->interrupt_threads) {
				break;
			}
		}
		cp->ir->resample_cleanup(cp->ir);
	}
	if (r >= 0) {
		cp->ir->prepare_convdata(cp->ir);
		cp->ir->init_conv(cp->ir);
	}
	cp->ir->reinit_running = 0;
	return NULL;
}

gint gui_load_timeout_callback(gpointer data) {
	struct control * cp = (struct control*)data;
	if (cp->ir->reinit_running) {
		ir_wavedisplay_set_progress(IR_WAVEDISPLAY(cp->wave_display), cp->ir->src_progress);
		return TRUE;
	}
	g_thread_join(cp->gui_load_thread);
	cp->gui_load_thread = NULL;
	ir_wavedisplay_set_progress(IR_WAVEDISPLAY(cp->wave_display), -1.0);
	ir_wavedisplay_set_message(IR_WAVEDISPLAY(cp->wave_display), NULL);
	refresh_gui_on_load(cp);
	reset_values(cp);
	cp->gui_load_timeout_tag = 0;
	return FALSE;
}

void gui_load_sndfile(struct control * cp, char * filename) {

	if (cp->ir->reinit_running || cp->gui_load_thread) {
		return;
	}

	if (cp->ir->source_path) {
		free(cp->ir->source_path);
	}
	cp->ir->source_path = strdup(filename);
	ir_wavedisplay_set_message(IR_WAVEDISPLAY(cp->wave_display), "Loading...");
	ir_wavedisplay_set_progress(IR_WAVEDISPLAY(cp->wave_display), 0.0);
	if (cp->ir->load_sndfile(cp->ir) < 0) {
		fprintf(stderr, "IR: load_sndfile error\n");
		ir_wavedisplay_set_message(IR_WAVEDISPLAY(cp->wave_display), NULL);
	} else {
		uint64_t hash = fhash(filename);
		float value0, value1, value2;
		ports_from_fhash(hash, &value0, &value1, &value2);
		cp->write_function(cp->controller, IR_PORT_FHASH_0, sizeof(float),
				   0 /* default format */, &value0);
		cp->write_function(cp->controller, IR_PORT_FHASH_1, sizeof(float),
				   0 /* default format */, &value1);
		cp->write_function(cp->controller, IR_PORT_FHASH_2, sizeof(float),
				   0 /* default format */, &value2);

		cp->ir->reinit_running = 1;
		cp->gui_load_thread = g_thread_new("gui_load_thread", gui_load_thread, cp);
		cp->gui_load_timeout_tag = g_timeout_add(100, gui_load_timeout_callback, cp);
	}
}

static void browse_button_clicked(GtkWidget * w, gpointer data) {

	struct control * cp = (struct control *)data;
	GtkWidget * dialog;
	if (cp->ir->reinit_running) {
		return;
	}
	dialog = gtk_file_chooser_dialog_new("Open File",
					     NULL,
					     GTK_FILE_CHOOSER_ACTION_OPEN,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					     NULL);

	GtkFileFilter * filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "All files");
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "Soundfiles");
	gtk_file_filter_add_pattern(filter, "*.wav");
	gtk_file_filter_add_pattern(filter, "*.WAV");
	gtk_file_filter_add_pattern(filter, "*.aiff");
	gtk_file_filter_add_pattern(filter, "*.AIFF");
	gtk_file_filter_add_pattern(filter, "*.au");
	gtk_file_filter_add_pattern(filter, "*.AU");
	gtk_file_filter_add_pattern(filter, "*.flac");
	gtk_file_filter_add_pattern(filter, "*.FLAC");
	gtk_file_filter_add_pattern(filter, "*.ogg");
	gtk_file_filter_add_pattern(filter, "*.OGG");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), filter);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char * filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
		gui_load_sndfile(cp, filename);
		char * dirname = g_path_get_dirname(filename);
		load_files(cp->store_files, dirname);
		GtkTreeSelection * select =
			gtk_tree_view_get_selection(GTK_TREE_VIEW(cp->tree_bookmarks));
		g_signal_handler_block(select, cp->bookmarks_sel_cbid);
		select_entry(cp->model_bookmarks, select, dirname);
		g_signal_handler_unblock(select, cp->bookmarks_sel_cbid);

		select = gtk_tree_view_get_selection(GTK_TREE_VIEW(cp->tree_files));
		g_signal_handler_block(select, cp->files_sel_cbid);
		select_entry(GTK_TREE_MODEL(cp->store_files), select, filename);
		g_signal_handler_unblock(select, cp->files_sel_cbid);
		g_free(filename);
		g_free(dirname);
	}
	gtk_widget_destroy(dialog);
}

int key_pressed_cb(GtkWidget * widget, GdkEventKey * event, gpointer data) {
	struct control * cp = (struct control *)data;
	if (cp->ir->reinit_running) {
		return FALSE;
	}
	cp->key_pressed = 1;
	return FALSE;
}

void save_value(struct control * cp, int port, float value) {
	switch (port) {
	case IR_PORT_PREDELAY: cp->predelay = value;
		break;
	case IR_PORT_ATTACK: cp->attack = value;
		break;
	case IR_PORT_ATTACKTIME: cp->attacktime = value;
		break;
	case IR_PORT_ENVELOPE: cp->envelope = value;
		break;
	case IR_PORT_LENGTH: cp->length = value;
		break;
	case IR_PORT_STRETCH: cp->stretch = value;
		break;
	case IR_PORT_STEREO_IR: cp->stereo_ir = value;
		break;
	}
}

void reset_values(struct control * cp) {
	set_adjustment(cp, cp->adj_predelay, cp->predelay);
	set_adjustment(cp, cp->adj_attack, cp->attack);
	set_adjustment(cp, cp->adj_attacktime, cp->attacktime);
	set_adjustment(cp, cp->adj_envelope, cp->envelope);
	set_adjustment(cp, cp->adj_length, cp->length);
	set_adjustment(cp, cp->adj_stretch, cp->stretch);
	set_adjustment(cp, cp->adj_stereo_ir, cp->stereo_ir);
}

void reset_value(struct control * cp, GtkAdjustment * adj) {
	if (adj == cp->adj_predelay) {
		set_adjustment(cp, adj, cp->predelay);
	} else if (adj == cp->adj_attack) {
		set_adjustment(cp, adj, cp->attack);
	} else if (adj == cp->adj_attacktime) {
		set_adjustment(cp, adj, cp->attacktime);
	} else if (adj == cp->adj_envelope) {
		set_adjustment(cp, adj, cp->envelope);
	} else if (adj == cp->adj_length) {
		set_adjustment(cp, adj, cp->length);
	} else if (adj == cp->adj_stretch) {
		set_adjustment(cp, adj, cp->stretch);
	} else if (adj == cp->adj_stereo_ir) {
		set_adjustment(cp, adj, cp->stereo_ir);
	}
}

int key_released_cb(GtkWidget * widget, GdkEventKey * event, gpointer data) {
	struct control * cp = (struct control *)data;
	GtkAdjustment * adj = NULL;
	int port = 0;

	cp->key_pressed = 0;
	if (cp->ir->reinit_running) {
		return FALSE;
	}

	if (widget == cp->scale_predelay) {
		adj = cp->adj_predelay;
		port = IR_PORT_PREDELAY;
	} else if (widget == cp->scale_attack) {
		adj = cp->adj_attack;
		port = IR_PORT_ATTACK;
	} else if (widget == cp->scale_attacktime) {
		adj = cp->adj_attacktime;
		port = IR_PORT_ATTACKTIME;
	} else if (widget == cp->scale_envelope) {
		adj = cp->adj_envelope;
		port = IR_PORT_ENVELOPE;
	} else if (widget == cp->scale_length) {
		adj = cp->adj_length;
		port = IR_PORT_LENGTH;
	} else if (widget == cp->scale_stretch) {
		adj = cp->adj_stretch;
		port = IR_PORT_STRETCH;
		cp->ir->resample_pending = 1;
	} else if (widget == cp->scale_stereo_ir) {
		adj = cp->adj_stereo_ir;
		port = IR_PORT_STEREO_IR;
	}

	if (port == 0) {
		return FALSE;
	}

	float value = get_adjustment(cp, adj);
	save_value(cp, port, value);

	//printf("on button_release adj value = %f\n", value);
	cp->write_function(cp->controller, port, sizeof(float),
			   0 /* default format */, &value);

	cp->ir->run = 0;
	cp->ir->reinit_pending = 1;
	return FALSE;
}

void update_envdisplay(struct control * cp) {
	int attack_time_s =
		get_adjustment(cp, cp->adj_attacktime) *
		(float)cp->ir->sample_rate / 1000.0;
	float attack_pc = get_adjustment(cp, cp->adj_attack);
	float env_pc = get_adjustment(cp, cp->adj_envelope);
	float length_pc = get_adjustment(cp, cp->adj_length);
	int reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cp->toggle_reverse)) ? 1 : 0;
	ir_wavedisplay_set_envparams(IR_WAVEDISPLAY(cp->wave_display),
				     attack_time_s, attack_pc,
				     env_pc, length_pc, reverse);
}

void adjustment_changed_cb(GtkAdjustment * adj, gpointer data) {
	struct control * cp = (struct control *)data;
	float value = get_adjustment(cp, adj);
	int port = 0;
	int update_ui = 0;
	int label_idx = 0;
	if (adj == cp->adj_predelay) {
		label_idx = ADJ_PREDELAY;
	} else if (adj == cp->adj_attack) {
		label_idx = ADJ_ATTACK;
		update_ui = 1;
	} else if (adj == cp->adj_attacktime) {
		label_idx = ADJ_ATTACK;
		update_ui = 1;
	} else if (adj == cp->adj_envelope) {
		label_idx = ADJ_ENVELOPE;
		update_ui = 1;
	} else if (adj == cp->adj_length) {
		label_idx = ADJ_LENGTH;
		update_ui = 1;
	} else if (adj == cp->adj_stretch) {
		label_idx = ADJ_STRETCH;
	} else if (adj == cp->adj_stereo_in) {
		label_idx = ADJ_STEREO_IN;
		port = IR_PORT_STEREO_IN;
	} else if (adj == cp->adj_stereo_ir) {
		label_idx = ADJ_STEREO_IR;
	} else if (adj == cp->adj_dry_gain) {
		label_idx = ADJ_DRY_GAIN;
		port = IR_PORT_DRY_GAIN;
	} else if (adj == cp->adj_wet_gain) {
		label_idx = ADJ_WET_GAIN;
		port = IR_PORT_WET_GAIN;
	}

	if (cp->ir->reinit_running && !port) {
		return;
	}

	set_label(cp, label_idx);

	if (port == 0) {
		if (cp->key_pressed) {
			save_value(cp, port, value);
			if (update_ui) {
				update_envdisplay(cp);
			}
		} else {
			reset_value(cp, adj);
		}
		return;
	}

	//printf("adj changed to %f\n", value);
	cp->write_function(cp->controller, port, sizeof(float),
			   0 /* default format */, &value);
}

void toggle_button_cb(GtkWidget * widget, gpointer data) {

	struct control * cp = (struct control *)data;
	if (cp->ir->reinit_running && (widget == cp->toggle_reverse)) {
		g_signal_handler_block(widget, cp->toggle_reverse_cbid);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
					     !(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))));
		g_signal_handler_unblock(widget, cp->toggle_reverse_cbid);
		return;
	}
	int port = 0;
	float value;
	const char * text;
	if (widget == cp->toggle_dry_sw) {
		port = IR_PORT_DRY_SW;
	} else if (widget == cp->toggle_wet_sw) {
		port = IR_PORT_WET_SW;
	} else if (widget == cp->toggle_reverse) {
		port = IR_PORT_REVERSE;
	}
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		value = 1.0f;
		text = "ON";
	} else {
		value = 0.0f;
		text = "off";
	}
	cp->write_function(cp->controller, port, sizeof(float),
			   0 /* default format */, &value);

	if (port == IR_PORT_REVERSE) {
		cp->ir->run = 0;
		cp->ir->reinit_pending = 1;
		update_envdisplay(cp);
	} else if ((port == IR_PORT_DRY_SW) ||
		   (port == IR_PORT_WET_SW)) {
		gtk_button_set_label(GTK_BUTTON(widget), text);
	}
}

void irctrl_add_row(GtkWidget * table, int row, GtkWidget ** label, GtkAdjustment * adj,
		    GtkWidget ** scale, int idx, gpointer data) {

	struct control * cp = (struct control *)data;
	*label = gtk_label_new("");
	gtk_label_set_justify(GTK_LABEL(*label), GTK_JUSTIFY_RIGHT);
	set_label(cp, idx);
	gtk_misc_set_alignment(GTK_MISC(*label), 1.0f, 0.0f);
	gtk_table_attach(GTK_TABLE(table), *label, 0, 1, row, row + 1,
			 GTK_FILL, GTK_FILL, 0, 0);

	*scale = gtk_hscale_new(adj);
	gtk_scale_set_draw_value(GTK_SCALE(*scale), FALSE);
	gtk_widget_add_events(*scale, GDK_BUTTON_RELEASE_MASK);
	g_signal_connect(*scale, "button_press_event",
			 G_CALLBACK(key_pressed_cb), data);
	g_signal_connect(*scale, "button_release_event",
			 G_CALLBACK(key_released_cb), data);
	gtk_table_attach_defaults(GTK_TABLE(table), *scale, 1, 3, row, row + 1);
}

void irctrl_add_row2(GtkWidget * table, int row,
		     GtkWidget ** label, GtkAdjustment * adj1, GtkAdjustment * adj2,
		     GtkWidget ** scale1, GtkWidget ** scale2,
		     int idx, gpointer data) {

	struct control * cp = (struct control *)data;
	*label = gtk_label_new("");
	gtk_label_set_justify(GTK_LABEL(*label), GTK_JUSTIFY_RIGHT);
	set_label(cp, idx);
	gtk_misc_set_alignment(GTK_MISC(*label), 1.0f, 0.0f);
	gtk_table_attach(GTK_TABLE(table), *label, 0, 1, row, row + 1,
			 GTK_FILL, GTK_FILL, 0, 0);

	*scale1 = gtk_hscale_new(adj1);
	gtk_scale_set_draw_value(GTK_SCALE(*scale1), FALSE);
	gtk_widget_add_events(*scale1, GDK_BUTTON_RELEASE_MASK);
	g_signal_connect(*scale1, "button_press_event",
			 G_CALLBACK(key_pressed_cb), data);
	g_signal_connect(*scale1, "button_release_event",
			 G_CALLBACK(key_released_cb), data);
	gtk_table_attach_defaults(GTK_TABLE(table), *scale1, 1, 2, row, row + 1);

	*scale2 = gtk_hscale_new(adj2);
	gtk_scale_set_draw_value(GTK_SCALE(*scale2), FALSE);
	gtk_widget_add_events(*scale2, GDK_BUTTON_RELEASE_MASK);
	g_signal_connect(*scale2, "button_press_event",
			 G_CALLBACK(key_pressed_cb), data);
	g_signal_connect(*scale2, "button_release_event",
			 G_CALLBACK(key_released_cb), data);
	gtk_table_attach_defaults(GTK_TABLE(table), *scale2, 2, 3, row, row + 1);
}

GtkWidget * make_irctrl_frame(struct control * cp) {

	GtkWidget * frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	
	GtkWidget * table = gtk_table_new(6, 3, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), PAD);
	gtk_table_set_col_spacings(GTK_TABLE(table), 2*PAD);
	gtk_container_add(GTK_CONTAINER(frame), table);

	/* Predelay */
	cp->adj_predelay = create_adjustment(ADJ_PREDELAY, cp);
	irctrl_add_row(table, 0, &cp->label_predelay, cp->adj_predelay, &cp->scale_predelay,
		       ADJ_PREDELAY, cp);

	/* Attack */
	cp->adj_attack = create_adjustment(ADJ_ATTACK, cp);
	cp->adj_attacktime = create_adjustment(ADJ_ATTACKTIME, cp);
	irctrl_add_row2(table, 1, &cp->label_attack, cp->adj_attack, cp->adj_attacktime,
			&cp->scale_attack, &cp->scale_attacktime,
			ADJ_ATTACK, cp);

	/* Envelope */
	cp->adj_envelope = create_adjustment(ADJ_ENVELOPE, cp);
	irctrl_add_row(table, 2, &cp->label_envelope, cp->adj_envelope, &cp->scale_envelope,
		       ADJ_ENVELOPE, cp);

	/* Length */
	cp->adj_length = create_adjustment(ADJ_LENGTH, cp);
	irctrl_add_row(table, 3, &cp->label_length, cp->adj_length, &cp->scale_length,
		       ADJ_LENGTH, cp);

	/* Stretch */
	cp->adj_stretch = create_adjustment(ADJ_STRETCH, cp);
	irctrl_add_row(table, 4, &cp->label_stretch, cp->adj_stretch, &cp->scale_stretch,
		       ADJ_STRETCH, cp);

	/* Stereo width in/IR */
	cp->adj_stereo_in = create_adjustment(ADJ_STEREO_IN, cp);
	cp->adj_stereo_ir = create_adjustment(ADJ_STEREO_IR, cp);
	irctrl_add_row2(table, 5, &cp->label_stereo, cp->adj_stereo_in, cp->adj_stereo_ir,
			&cp->scale_stereo_in, &cp->scale_stereo_ir,
			ADJ_STEREO_IN, cp);

	gtk_scale_add_mark(GTK_SCALE(cp->scale_stretch), 100.0, GTK_POS_BOTTOM, XS1 " " XS2);
	gtk_scale_add_mark(GTK_SCALE(cp->scale_stereo_in), 100.0, GTK_POS_BOTTOM, XS1 " " XS2);
	gtk_scale_add_mark(GTK_SCALE(cp->scale_stereo_ir), 100.0, GTK_POS_BOTTOM, XS1 " " XS2);

	gtk_widget_set_size_request(cp->scale_attack, 125, -1);
	gtk_widget_set_size_request(cp->scale_attacktime, 125, -1);
	return frame;
}

void agc_toggle_cb(GtkWidget * widget, gpointer data) {
	struct control * cp = (struct control *)data;
	float value;
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		value = 1.0f;
	} else {
		value = 0.0f;
	}

	cp->write_function(cp->controller, IR_PORT_AGC_SW, sizeof(float),
			   0 /* default format */, &value);
	set_agc_label(cp);
}

GtkWidget * make_mixer_frame(struct control * cp) {

	GtkWidget * frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	
	GtkWidget * vbox_top = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox_top);

	GtkWidget * hbox = gtk_hbox_new(TRUE, PAD);
	gtk_box_pack_start(GTK_BOX(vbox_top), hbox, TRUE, TRUE, PAD);

	cp->adj_dry_gain = create_adjustment(ADJ_DRY_GAIN, cp);
	cp->adj_wet_gain = create_adjustment(ADJ_WET_GAIN, cp);

	/* Dry */
	GtkWidget * vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, PAD);

	GtkWidget * label = gtk_label_new("");
	gtk_label_set_markup(GTK_LABEL(label), S1 "<b>Dry</b>" S2);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, PAD);

	GtkWidget * hbox_i = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox_i, TRUE, TRUE, PAD);

	GtkWidget * scale;
	scale = gtk_vscale_new(cp->adj_dry_gain);
	gtk_range_set_inverted(GTK_RANGE(scale), TRUE);
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_scale_add_mark(GTK_SCALE(scale), convert_real_to_scale(ADJ_DRY_GAIN, 0.0),
			   GTK_POS_RIGHT, XS1 " " XS2);
	gtk_box_pack_start(GTK_BOX(hbox_i), scale, TRUE, TRUE, 0);

	cp->meter_L_dry = ir_meter_new();
	gtk_widget_set_size_request(cp->meter_L_dry, 5, -1);
	gtk_box_pack_start(GTK_BOX(hbox_i), cp->meter_L_dry, FALSE, TRUE, 1);

	cp->meter_R_dry = ir_meter_new();
	gtk_widget_set_size_request(cp->meter_R_dry, 5, -1);
	gtk_box_pack_start(GTK_BOX(hbox_i), cp->meter_R_dry, FALSE, TRUE, 0);


	cp->label_dry_gain = gtk_label_new("0.0 dB");
	gtk_box_pack_start(GTK_BOX(vbox), cp->label_dry_gain, FALSE, TRUE, PAD);

	cp->toggle_dry_sw = gtk_toggle_button_new_with_label("off");
	g_signal_connect(cp->toggle_dry_sw, "toggled",
			 G_CALLBACK(toggle_button_cb), cp);
	gtk_box_pack_start(GTK_BOX(vbox), cp->toggle_dry_sw, FALSE, FALSE, PAD);

	/* Wet */
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, PAD);

	label = gtk_label_new("");
	gtk_label_set_markup(GTK_LABEL(label), S1 "<b>Wet</b>" S2);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, PAD);

	hbox_i = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox_i, TRUE, TRUE, PAD);

	scale = gtk_vscale_new(cp->adj_wet_gain);
	gtk_range_set_inverted(GTK_RANGE(scale), TRUE);
	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	gtk_scale_add_mark(GTK_SCALE(scale), convert_real_to_scale(ADJ_WET_GAIN, 0.0),
			   GTK_POS_RIGHT, XS1 " " XS2);
	gtk_box_pack_start(GTK_BOX(hbox_i), scale, TRUE, TRUE, 0);

	cp->meter_L_wet = ir_meter_new();
	gtk_widget_set_size_request(cp->meter_L_wet, 5, -1);
	gtk_box_pack_start(GTK_BOX(hbox_i), cp->meter_L_wet, FALSE, TRUE, 1);

	cp->meter_R_wet = ir_meter_new();
	gtk_widget_set_size_request(cp->meter_R_wet, 5, -1);
	gtk_box_pack_start(GTK_BOX(hbox_i), cp->meter_R_wet, FALSE, TRUE, 0);


	cp->label_wet_gain = gtk_label_new("-6.0 dB");
	gtk_box_pack_start(GTK_BOX(vbox), cp->label_wet_gain, FALSE, TRUE, PAD);

	cp->toggle_wet_sw = gtk_toggle_button_new_with_label("off");
	g_signal_connect(cp->toggle_wet_sw, "toggled",
			 G_CALLBACK(toggle_button_cb), cp);
	gtk_box_pack_start(GTK_BOX(vbox), cp->toggle_wet_sw, FALSE, FALSE, PAD);

	/* Autogain */
	hbox = gtk_hbox_new(FALSE, PAD);
	gtk_box_pack_start(GTK_BOX(vbox_top), hbox, FALSE, TRUE, 0);
	cp->toggle_agc_sw = gtk_toggle_button_new_with_label("Autogain");
	g_signal_connect(cp->toggle_agc_sw, "toggled",
			 G_CALLBACK(agc_toggle_cb), cp);
	gtk_box_pack_start(GTK_BOX(hbox), cp->toggle_agc_sw, TRUE, TRUE, PAD);
	
	return frame;
}

static void add_bookmark_button_clicked(GtkWidget * w, gpointer data) {
	struct control * cp = (struct control *)data;
	GtkWidget * dialog;
	dialog = gtk_file_chooser_dialog_new("Select directory",
					     NULL,
					     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
					     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					     NULL);

	GtkWidget * hbox = gtk_hbox_new(FALSE, PAD);
	GtkWidget * label = gtk_label_new("Bookmark name (optional):");
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, PAD);
	GtkWidget * entry = gtk_entry_new();
	gtk_widget_show(entry);
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, PAD);
	gtk_widget_show(hbox);
	gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog), hbox);

	if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
		char * filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		const gchar * bookmark = gtk_entry_get_text(GTK_ENTRY(entry));
		char * name;

		if ((bookmark != NULL) && (strlen(bookmark) > 0)) {
			name = strdup(bookmark);
		} else {
			/* use last path component as name */
			name = g_path_get_basename(filename);
		}

		char * path = lookup_bookmark_in_store(cp->model_bookmarks, name);
		if (path) {
			fprintf(stderr, "IR: bookmark already exists!\n");
			g_free(path);
		} else {
			GtkTreeIter iter;
			gtk_list_store_append(cp->ir->store_bookmarks, &iter);
			gtk_list_store_set(cp->ir->store_bookmarks, &iter,
					   0, name, 1, filename, -1);
			store_bookmark(cp->ir->keyfile, name, filename);
		}
		g_free(name);
		g_free(filename);
	}
	gtk_widget_destroy(dialog);
}

static void del_bookmark_button_clicked(GtkWidget * w, gpointer data) {
	struct control * cp = (struct control *)data;
        GtkTreeIter iter; /* on sorted model */
        GtkTreeIter real_iter; /* on underlying model */
        GtkTreeModel * model;
	GtkTreeSelection * select;
        gchar * key;

	select = gtk_tree_view_get_selection(GTK_TREE_VIEW(cp->tree_bookmarks));
        if (gtk_tree_selection_get_selected(select, &model, &iter)) {
                gtk_tree_model_get(model, &iter, 0, &key, -1);
		delete_bookmark(cp->ir->keyfile, key);
		gtk_tree_model_sort_convert_iter_to_child_iter(
			GTK_TREE_MODEL_SORT(cp->model_bookmarks),
			&real_iter, &iter);
		gtk_list_store_remove(cp->ir->store_bookmarks, &real_iter);
                g_free(key);
        }
}

static void bookmarks_selection_changed_cb(GtkTreeSelection * selection, gpointer data) {
	struct control * cp = (struct control *)data;
        GtkTreeIter iter;
        GtkTreeModel * model;
        gchar * bookmark;
        gchar * dirpath;

        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
                gtk_tree_model_get(model, &iter, 0, &bookmark, 1, &dirpath, -1);
		load_files(cp->store_files, dirpath);
                g_free(bookmark);
                g_free(dirpath);
        }
}

static void files_selection_changed_cb(GtkTreeSelection * selection, gpointer data) {

	struct control * cp = (struct control *)data;

        GtkTreeIter iter;
        GtkTreeModel * model;
        gchar * filename;

        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
                gtk_tree_model_get(model, &iter, 1, &filename, -1);
		if (g_file_test(filename, G_FILE_TEST_IS_DIR)) {
			load_files(cp->store_files, filename);
			/* clear bookmark selection so it is clickable again
			 * for fast upwards navigation */
			gtk_tree_selection_unselect_all(
				gtk_tree_view_get_selection(GTK_TREE_VIEW(cp->tree_bookmarks)));
		} else {
			gui_load_sndfile(cp, filename);
		}
                g_free(filename);
        }
}

void tree_view_realized_cb(GtkWidget * widget, gpointer data) {

	struct control * cp = (struct control *)data;

	if (widget == cp->tree_bookmarks) {
		cp->bookmarks_realized = 1;
	} else if (widget == cp->tree_files) {
		cp->files_realized = 1;
	}

	if (cp->bookmarks_realized && cp->files_realized && cp->ir->source_path) { 
		/* select appropriate entries if plugin loaded */
		char * dirpath = g_path_get_dirname(cp->ir->source_path);
		load_files(cp->store_files, dirpath);

		GtkTreeSelection * select =
			gtk_tree_view_get_selection(GTK_TREE_VIEW(cp->tree_bookmarks));
		g_signal_handler_block(select, cp->bookmarks_sel_cbid);
		select_entry(cp->model_bookmarks, select, dirpath);
		g_signal_handler_unblock(select, cp->bookmarks_sel_cbid);

		select = gtk_tree_view_get_selection(GTK_TREE_VIEW(cp->tree_files));
		g_signal_handler_block(select, cp->files_sel_cbid);
		select_entry(GTK_TREE_MODEL(cp->store_files), select, cp->ir->source_path);
		g_signal_handler_unblock(select, cp->files_sel_cbid);

		g_free(dirpath);

		refresh_gui_on_load(cp);
	}
}

GtkWidget * make_lists_box(struct control * cp) {

	GtkWidget * hbox = gtk_hbox_new(FALSE, 0);
	GtkWidget * vbox = gtk_vbox_new(FALSE, 0);

	GtkTreeModel * model = GTK_TREE_MODEL(cp->ir->store_bookmarks);
	cp->model_bookmarks = gtk_tree_model_sort_new_with_model(model);
	cp->tree_bookmarks = gtk_tree_view_new_with_model(cp->model_bookmarks);
	g_signal_connect(G_OBJECT(cp->tree_bookmarks), "realize",
			 G_CALLBACK(tree_view_realized_cb), cp);
	GtkWidget * scroll_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_win),
				       GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_container_add(GTK_CONTAINER(scroll_win), cp->tree_bookmarks);
	gtk_box_pack_start(GTK_BOX(vbox), scroll_win, TRUE, TRUE, 0);
	
	GtkCellRenderer * renderer;
	GtkTreeViewColumn * column;
	GtkTreeSelection * select_b;
	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "scale", 0.8, NULL);
	g_object_set(renderer, "scale-set", TRUE, NULL);
	column = gtk_tree_view_column_new_with_attributes("Bookmarks", renderer, "text", 0, NULL);
	gtk_tree_view_column_set_sort_column_id(column, 0);
	gtk_tree_view_column_set_sort_indicator(column, TRUE);
	gtk_tree_view_column_set_sort_order(column, GTK_SORT_ASCENDING);
	gtk_tree_view_append_column(GTK_TREE_VIEW(cp->tree_bookmarks), column);
	gtk_tree_view_column_clicked(column);

	select_b = gtk_tree_view_get_selection(GTK_TREE_VIEW(cp->tree_bookmarks));
	gtk_tree_selection_set_mode(select_b, GTK_SELECTION_SINGLE);
	cp->bookmarks_sel_cbid =
		g_signal_connect(G_OBJECT(select_b), "changed",
				 G_CALLBACK(bookmarks_selection_changed_cb), cp);

	GtkWidget * hbox_1 = gtk_hbox_new(TRUE, PAD);
	gtk_box_pack_start(GTK_BOX(vbox), hbox_1, FALSE, FALSE, PAD);

	GtkWidget * button = gtk_button_new_with_label("Add...");
	g_signal_connect(G_OBJECT(button), "clicked", 
			 G_CALLBACK(add_bookmark_button_clicked), cp);
	gtk_box_pack_start(GTK_BOX(hbox_1), button, TRUE, TRUE, PAD);

	button = gtk_button_new_with_label("Remove");
	g_signal_connect(G_OBJECT(button), "clicked", 
			 G_CALLBACK(del_bookmark_button_clicked), cp);
	gtk_box_pack_start(GTK_BOX(hbox_1), button, TRUE, TRUE, PAD);

	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, PAD);

	vbox = gtk_vbox_new(FALSE, 0);

	cp->store_files = gtk_list_store_new(2,
					     G_TYPE_STRING,  /* visible name (key) */
					     G_TYPE_STRING); /* full pathname (value) */
	cp->tree_files = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cp->store_files));
	g_signal_connect(G_OBJECT(cp->tree_files), "realize",
			 G_CALLBACK(tree_view_realized_cb), cp);
	scroll_win = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_win),
				       GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_container_add(GTK_CONTAINER(scroll_win), cp->tree_files);
	gtk_box_pack_start(GTK_BOX(vbox), scroll_win, TRUE, TRUE, 0);

	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "scale", 0.8, NULL);
	g_object_set(renderer, "scale-set", TRUE, NULL);
	column = gtk_tree_view_column_new_with_attributes("Files", renderer, "text", 0, NULL);
	gtk_tree_view_column_set_sort_column_id(column, 0);
	gtk_tree_view_column_set_sort_indicator(column, TRUE);
	gtk_tree_view_column_set_sort_order(column, GTK_SORT_ASCENDING);
	gtk_tree_view_append_column(GTK_TREE_VIEW(cp->tree_files), column);
	gtk_tree_view_column_clicked(column);
	GtkTreeSelection * select_f;
	select_f = gtk_tree_view_get_selection(GTK_TREE_VIEW(cp->tree_files));
	gtk_tree_selection_set_mode(select_f, GTK_SELECTION_SINGLE);
	cp->files_sel_cbid =
		g_signal_connect(G_OBJECT(select_f), "changed",
				 G_CALLBACK(files_selection_changed_cb), cp);

	GtkWidget * browse_button = gtk_button_new_with_label("Open File...");
	gtk_box_pack_start(GTK_BOX(vbox), browse_button, FALSE, FALSE, PAD);
	g_signal_connect(G_OBJECT(browse_button), "clicked", 
			 G_CALLBACK(browse_button_clicked), cp);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, PAD);

	return hbox;
}

gpointer reinit_thread(gpointer data) {
	struct control * cp = (struct control*)data;
	if (cp->ir->resample_pending) {
		int r = cp->ir->resample_init(cp->ir);
		if (r == 0) {
			while (r == 0) {
				r = cp->ir->resample_do(cp->ir);
				if (cp->interrupt_threads) {
					break;
				}
			}
			cp->ir->resample_cleanup(cp->ir);
		}
		cp->ir->resample_pending = 0;
	}
	cp->ir->prepare_convdata(cp->ir);
	cp->ir->init_conv(cp->ir);
	cp->ir->reinit_pending = 0;
	cp->ir->reinit_running = 0;
	return NULL;
}

gint reinit_timeout_callback(gpointer data) {
	struct control * cp = (struct control*)data;
	if (!cp->ir->ir_samples || !cp->ir->ir_nfram) {
		ir_wavedisplay_set_message(IR_WAVEDISPLAY(cp->wave_display), NULL);
		cp->reinit_timeout_tag = 0;
		return FALSE;
	}
	if (cp->ir->reinit_running) {
		if (cp->ir->resample_pending) {
			ir_wavedisplay_set_progress(IR_WAVEDISPLAY(cp->wave_display),
						    cp->ir->src_progress);
		}
		return TRUE;
	}
	g_thread_join(cp->reinit_thread);
	cp->reinit_thread = NULL;
	ir_wavedisplay_set_progress(IR_WAVEDISPLAY(cp->wave_display), -1.0);
	ir_wavedisplay_set_message(IR_WAVEDISPLAY(cp->wave_display), NULL);
	ir_wavedisplay_set_wave(IR_WAVEDISPLAY(cp->wave_display),
				cp->ir->ir_samples[cp->disp_chan],
				cp->ir->ir_nfram);
	reset_values(cp);
	cp->reinit_timeout_tag = 0;
	return FALSE;
}

gint timeout_callback(gpointer data) {
	struct control * cp = (struct control*)data;
	if (cp->interrupt_threads) {
		cp->timeout_tag = 0;
		return FALSE;
	}
	if (cp->ir->reinit_running) {
		return TRUE;
	}
	if (cp->ir->run && cp->ir->reinit_pending) {
		if (cp->ir->resample_pending) {
			ir_wavedisplay_set_progress(IR_WAVEDISPLAY(cp->wave_display), 0.0);
		}
		ir_wavedisplay_set_message(IR_WAVEDISPLAY(cp->wave_display), "Calculating...");
		cp->ir->reinit_running = 1;
		cp->reinit_thread = g_thread_new("reinit_thread", reinit_thread, cp);
		cp->reinit_timeout_tag = g_timeout_add(100, reinit_timeout_callback, cp);
		cp->ir->run = 0;
	}
	return TRUE;
}

void chan_toggle_cb(GtkWidget * widget, gpointer data) {

	struct control * cp = (struct control *)data;
	int i;
	for (i = 0; i < 4; i++) {
		if (widget == cp->chan_toggle[i]) {
			break;
		}
	}
	if (cp->ir->reinit_running) {
		g_signal_handler_block(widget, cp->chan_toggle_cbid[i]);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
					     !(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))));
		g_signal_handler_unblock(widget, cp->chan_toggle_cbid[i]);
		return;
	}
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		for (int j = 0; j < 4; j++) {
			if (i != j) {
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cp->chan_toggle[j]), FALSE);
			}
		}
		cp->disp_chan = i;
		if (cp->ir->ir_nfram) {
			ir_wavedisplay_set_wave(IR_WAVEDISPLAY(cp->wave_display),
						cp->ir->ir_samples[i], cp->ir->ir_nfram);
		}
	}
}

void log_toggle_cb(GtkWidget * widget, gpointer data) {
	struct control * cp = (struct control *)data;
	if (cp->ir->reinit_running) {
		g_signal_handler_block(widget, cp->log_toggle_cbid);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
					     !(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))));
		g_signal_handler_unblock(widget, cp->log_toggle_cbid);
		return;
	}
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
		gtk_button_set_label(GTK_BUTTON(widget), " log ");
		ir_wavedisplay_set_logarithmic(IR_WAVEDISPLAY(cp->wave_display), 1);
	} else {
		gtk_button_set_label(GTK_BUTTON(widget), " lin ");
		ir_wavedisplay_set_logarithmic(IR_WAVEDISPLAY(cp->wave_display), 0);
	}
}

static void about_button_cb(GtkWidget * about_button, gpointer data) {

	GtkWidget * dialog;
	GtkWidget * frame = gtk_frame_new(NULL);
	GtkWidget * label = gtk_label_new("");
	GtkWidget * content_area;

	dialog = gtk_dialog_new_with_buttons("About IR",
					     NULL,
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_OK,
					     GTK_RESPONSE_NONE,
					     NULL);
	content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_label_set_markup(GTK_LABEL(label),
			     "<span size=\"x-large\" weight=\"heavy\">"
			     "IR</span><span size=\"x-large\">: LV2 Convolution Reverb\n"
			     "</span>"
			     S1 "version 1.2.2" S2
			     "\n\nCopyright (C) 2011-2012 <b>Tom Szilagyi</b>\n"
			     XS1 "\nIR is free software under the GNU GPL. There is ABSOLUTELY\n"
			     "NO WARRANTY, not even for MERCHANTABILITY or FITNESS\n"
			     "FOR A PARTICULAR PURPOSE." XS2 "\n\n"
			     "<small>Homepage: <b>http://tomszilagyi.github.io/plugins/ir.lv2</b></small>");
	gtk_label_set_selectable(GTK_LABEL(label), TRUE);
	gtk_container_add(GTK_CONTAINER(frame), label);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 2*PAD);

	g_signal_connect_swapped(dialog, "response",
				 G_CALLBACK(gtk_widget_destroy),
				 dialog);

	gtk_container_add(GTK_CONTAINER(content_area), frame);
	gtk_container_set_border_width(GTK_CONTAINER(dialog), 2*PAD);
	gtk_widget_show_all(dialog);
}

GtkWidget * make_top_hbox(struct control * cp) {
	GtkWidget * hbox = gtk_hbox_new(FALSE, PAD);
	GtkWidget * frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, PAD);
	GtkWidget * vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	GtkWidget * hbox_wave = gtk_hbox_new(FALSE, PAD);
	gtk_box_pack_start(GTK_BOX(vbox), hbox_wave, TRUE, TRUE, PAD);
	
	GtkWidget * vbox_toggle = gtk_vbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox_wave), vbox_toggle, FALSE, TRUE, PAD);

	for (int i = 0; i < 4; i++) {
		char str[4];
		snprintf(str, 4, " %d ", i+1);
		cp->chan_toggle[i] = gtk_toggle_button_new_with_label(str);
		cp->chan_toggle_cbid[i] = g_signal_connect(cp->chan_toggle[i], "toggled",
							   G_CALLBACK(chan_toggle_cb), cp);
		gtk_box_pack_start(GTK_BOX(vbox_toggle), cp->chan_toggle[i], TRUE, TRUE, PAD);
		gtk_widget_set_sensitive(cp->chan_toggle[i], FALSE);
	}

	cp->wave_display = ir_wavedisplay_new();
	gtk_box_pack_start(GTK_BOX(hbox_wave), cp->wave_display, TRUE, TRUE, 0);

	cp->mode_ind = ir_modeind_new();
	gtk_widget_set_size_request(cp->mode_ind, 100, -1);
	gtk_box_pack_start(GTK_BOX(hbox_wave), cp->mode_ind, FALSE, FALSE, PAD);

	GtkWidget * hbox2 = gtk_hbox_new(FALSE, PAD);
	gtk_box_pack_start(GTK_BOX(vbox), hbox2, FALSE, FALSE, PAD);

	cp->log_toggle = gtk_toggle_button_new_with_label(" lin ");
	cp->log_toggle_cbid = g_signal_connect(cp->log_toggle, "toggled",
					       G_CALLBACK(log_toggle_cb), cp);
	gtk_widget_set_size_request(cp->log_toggle, 50, -1);
	gtk_box_pack_start(GTK_BOX(hbox2), cp->log_toggle, FALSE, TRUE, PAD);

	cp->wave_annot_label = gtk_label_new("");
	gtk_misc_set_alignment(GTK_MISC(cp->wave_annot_label), 0.0f, 0.5f);
	gtk_box_pack_start(GTK_BOX(hbox2), cp->wave_annot_label, TRUE, TRUE, PAD);

	GtkWidget * about_button = gtk_button_new_with_label(" About ");
	g_signal_connect(about_button, "clicked",
			 G_CALLBACK(about_button_cb), cp);
	gtk_box_pack_start(GTK_BOX(hbox2), about_button, FALSE, TRUE, PAD);

	return hbox;
}

void make_gui_proper(struct control * cp) {

	GtkWidget * vbox_top = cp->vbox_top;

	cp->toggle_reverse = gtk_toggle_button_new_with_label("Reverse");
	cp->toggle_reverse_cbid = g_signal_connect(cp->toggle_reverse, "toggled",
						   G_CALLBACK(toggle_button_cb), cp);

	/* upper half */
	gtk_box_pack_start(GTK_BOX(vbox_top), make_top_hbox(cp), TRUE, TRUE, PAD);

	GtkWidget * hbox = gtk_hbox_new(FALSE, PAD);
	gtk_box_pack_start(GTK_BOX(vbox_top), hbox, TRUE, TRUE, 0);

	GtkWidget * hpaned = gtk_hpaned_new();
	GtkWidget * hbox_1 = gtk_hbox_new(FALSE, PAD);
	gtk_paned_pack1(GTK_PANED(hpaned), hbox_1, TRUE, TRUE);
	gtk_box_pack_start(GTK_BOX(hbox_1), make_lists_box(cp), TRUE, TRUE, 0);

	GtkWidget * hbox_2 = gtk_hbox_new(FALSE, 0);
	gtk_paned_pack2(GTK_PANED(hpaned), hbox_2, TRUE, FALSE);
	GtkWidget * vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), make_irctrl_frame(cp), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), cp->toggle_reverse, FALSE, TRUE, PAD);
	gtk_box_pack_start(GTK_BOX(hbox_2), vbox, TRUE, TRUE, PAD);
	gtk_box_pack_start(GTK_BOX(hbox_2), make_mixer_frame(cp), FALSE, TRUE, PAD);

	gtk_box_pack_start(GTK_BOX(hbox), hpaned, TRUE, TRUE, 0);

	cp->timeout_tag = g_timeout_add(100, timeout_callback, cp);

	gtk_widget_show_all(vbox_top);
}

static void port_event(LV2UI_Handle ui, uint32_t port_index, uint32_t buffer_size,
		       uint32_t format, const void * buffer);

void replay_func(gpointer data, gpointer user_data) {
	port_event_t * pe = (port_event_t *)data;
	struct control * cp = (struct control*)user_data;
	port_event((LV2UI_Handle)cp, pe->port_index, 0, 0, &pe->value);
	free(pe);
}

void replay_port_events(struct control * cp) {
	GSList * q = cp->port_event_q;
	g_slist_foreach(q, replay_func, cp);
	g_slist_free(q);
}

gint waitplugin_timeout_callback(gpointer data) {
	struct control * cp = (struct control*)data;
	if (cp->ir->first_conf_done) {
		gtk_widget_destroy(cp->hbox_waitplugin);
		make_gui_proper(cp);
		replay_port_events(cp);
		cp->waitplugin_timeout_tag = 0;
		return FALSE;
	}
	if (cp->interrupt_threads) {
		cp->waitplugin_timeout_tag = 0;
		return FALSE;
	}
	return TRUE;
}

GtkWidget * make_gui(struct control * cp) {

	cp->toggle_reverse = gtk_toggle_button_new_with_label("Reverse");
	g_signal_connect(cp->toggle_reverse, "toggled",
			 G_CALLBACK(toggle_button_cb), cp);

	cp->vbox_top = gtk_vbox_new(FALSE, PAD);

	if (cp->ir->first_conf_done) {
		make_gui_proper(cp);
	} else {
		cp->hbox_waitplugin = gtk_hbox_new(FALSE, PAD);
		gtk_box_pack_start(GTK_BOX(cp->vbox_top), cp->hbox_waitplugin, TRUE, TRUE, PAD);
#ifdef _HAVE_GTK_ATLEAST_2_20
		GtkWidget * spinner = gtk_spinner_new();
		gtk_spinner_start(GTK_SPINNER(spinner));
		gtk_box_pack_start(GTK_BOX(cp->hbox_waitplugin), spinner, TRUE, TRUE, PAD);
#endif /* _HAVE_GTK_ATLEAST_2_20 */
		GtkWidget * label = gtk_label_new("");
		gtk_label_set_markup(GTK_LABEL(label),
				     "<span size=\"large\" weight=\"bold\">"
				     " Please wait while plugin is initialised... "
				     "</span>\n"
				     XS1 "  If the plugin is in BYPASS (Deactivated), please un-BYPASS (Activate) it." XS2);
		gtk_box_pack_start(GTK_BOX(cp->hbox_waitplugin), label, TRUE, TRUE, PAD);
		cp->waitplugin_timeout_tag = g_timeout_add(100, waitplugin_timeout_callback, cp);
		gtk_widget_show_all(cp->vbox_top);
	}
	return cp->vbox_top;
}

/* join any threads and wait for timeouts to exit */
void join_timeouts(struct control * cp) {
	cp->interrupt_threads = 1;
	while (cp->timeout_tag ||
	       cp->gui_load_timeout_tag ||
	       cp->reinit_timeout_tag ||
	       cp->waitplugin_timeout_tag) {
		
		gtk_main_iteration();
	}
}

static void cleanup(LV2UI_Handle ui) {
	//printf("cleanup()\n");
	struct control * cp = (struct control *)ui;

	join_timeouts(cp);
	if (cp->store_files) {
		g_object_unref(cp->store_files);
		cp->store_files = 0;
	}
	free(cp);
}

static LV2UI_Handle instantiate(const struct _LV2UI_Descriptor * descriptor,
				const char * plugin_uri,
				const char * bundle_path,
				LV2UI_Write_Function write_function,
				LV2UI_Controller controller,
				LV2UI_Widget * widget,
				const LV2_Feature * const * features) {

	int instance_access_found = 0;
	struct control * cp;
	//printf("instantiate('%s', '%s') called\n", plugin_uri, bundle_path);
	
	if (strcmp(plugin_uri, IR_URI) != 0) {
		fprintf(stderr, "IR_UI error: this GUI does not support plugin with URI %s\n", plugin_uri);
		goto fail;
	}

	cp = (struct control*)calloc(1, sizeof(struct control));
	if (cp == NULL) {
		goto fail;
	}

	if (features != NULL) {
		int i = 0;
		while (features[i] != NULL) {
			if (strcmp(features[i]->URI, LV2_INSTANCE_ACCESS_URI) == 0) {
				cp->ir = (IR *)(features[i]->data);
				instance_access_found = 1;
			}
			++i;
		}
		if (!instance_access_found) {
			goto fail_free;
		}
	} else {
		goto fail_free;
	}

	if (cp->ir == NULL) {
		goto fail_free;
	}

	cp->controller = controller;
	cp->write_function = write_function;

	*widget = (LV2UI_Widget)make_gui(cp);
	return (LV2UI_Handle)cp;

 fail_free:
	if (!instance_access_found) {
		fprintf(stderr, "IR UI: error: required LV2 feature %s missing!\n", LV2_INSTANCE_ACCESS_URI);
	}

	free(cp);
 fail:
	return NULL;
}

static void port_event(LV2UI_Handle ui,
		       uint32_t port_index,
		       uint32_t buffer_size,
		       uint32_t format,
		       const void * buffer) {

	struct control * cp = (struct control *)ui;
	float * pval = (float *)buffer;
	//printf("port_event(%u, %f) called\n", (unsigned int)port_index, *(float *)buffer);

	if (format != 0) {
		return;
	}

	if (!cp->ir->first_conf_done) {
		port_event_t * pe = (port_event_t *)malloc(sizeof(port_event_t));
		pe->port_index = port_index;
		pe->value = *pval;
		cp->port_event_q = g_slist_prepend(cp->port_event_q, pe);
		return;
	}

	int update_ui = 0;
	if (port_index == IR_PORT_REVERSE) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cp->toggle_reverse),
					     (*pval > 0.0f));
		update_ui = 1;
	} else if (port_index == IR_PORT_PREDELAY) {
		cp->predelay = *pval;
		set_adjustment(cp, cp->adj_predelay, *pval);
		update_ui = 1;
	} else if (port_index == IR_PORT_ATTACK) {
		cp->attack = *pval;
		set_adjustment(cp, cp->adj_attack, *pval);
		update_ui = 1;
	} else if (port_index == IR_PORT_ATTACKTIME) {
		cp->attacktime = *pval;
		set_adjustment(cp, cp->adj_attacktime, *pval);
		update_ui = 1;
	} else if (port_index == IR_PORT_ENVELOPE) {
		cp->envelope = *pval;
		set_adjustment(cp, cp->adj_envelope, *pval);
		update_ui = 1;
	} else if (port_index == IR_PORT_LENGTH) {
		cp->length = *pval;
		set_adjustment(cp, cp->adj_length, *pval);
		update_ui = 1;
	} else if (port_index == IR_PORT_STRETCH) {
		cp->stretch = *pval;
		set_adjustment(cp, cp->adj_stretch, *pval);
		update_ui = 1;
	} else if (port_index == IR_PORT_STEREO_IN) {
		set_adjustment(cp, cp->adj_stereo_in, *pval);
	} else if (port_index == IR_PORT_STEREO_IR) {
		cp->stereo_ir = *pval;
		set_adjustment(cp, cp->adj_stereo_ir, *pval);
	} else if (port_index == IR_PORT_AGC_SW) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cp->toggle_agc_sw),
					     (*pval > 0.0f));
	} else if (port_index == IR_PORT_DRY_SW) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cp->toggle_dry_sw),
					     (*pval > 0.0f));
	} else if (port_index == IR_PORT_DRY_GAIN) {
		set_adjustment(cp, cp->adj_dry_gain, *pval);
	} else if (port_index == IR_PORT_WET_SW) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cp->toggle_wet_sw),
					     (*pval > 0.0f));
	} else if (port_index == IR_PORT_WET_GAIN) {
		set_adjustment(cp, cp->adj_wet_gain, *pval);
	} else if (port_index == IR_PORT_FHASH_0) { /* NOP: plugin itself handles IR loading on session resume */
	} else if (port_index == IR_PORT_FHASH_1) { /* NOP */
	} else if (port_index == IR_PORT_FHASH_2) { /* NOP */
	} else if (port_index == IR_PORT_METER_DRY_L) {
		ir_meter_set_level(IR_METER(cp->meter_L_dry), convert_real_to_scale(ADJ_DRY_GAIN, CO_DB(*pval)));
	} else if (port_index == IR_PORT_METER_DRY_R) {
		ir_meter_set_level(IR_METER(cp->meter_R_dry), convert_real_to_scale(ADJ_DRY_GAIN, CO_DB(*pval)));
	} else if (port_index == IR_PORT_METER_WET_L) {
		ir_meter_set_level(IR_METER(cp->meter_L_wet), convert_real_to_scale(ADJ_WET_GAIN, CO_DB(*pval)));
	} else if (port_index == IR_PORT_METER_WET_R) {
		ir_meter_set_level(IR_METER(cp->meter_R_wet), convert_real_to_scale(ADJ_WET_GAIN, CO_DB(*pval)));
	}

	if (update_ui) {
		update_envdisplay(cp);
	}
}

static LV2UI_Descriptor descriptors[] = {
	{IR_UI_URI, instantiate, cleanup, port_event, NULL}
};

const LV2UI_Descriptor * lv2ui_descriptor(uint32_t index) {
	//printf("lv2ui_descriptor(%u) called\n", (unsigned int)index);	
	if (index >= sizeof(descriptors) / sizeof(descriptors[0])) {
		return NULL;
	}
	return descriptors + index;
}
