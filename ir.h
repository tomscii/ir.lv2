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

#ifndef _IR_H
#define _IR_H

#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif

#include <math.h>
#include <gtk/gtk.h>
#include <sndfile.h>
#include <samplerate.h>
#include <zita-convolver.h>

#define IR_URI                    "http://tomszilagyi.github.io/plugins/lv2/ir"

#define BSIZE       0x4000      /* Blocksize for soundfile data access */
#define BSIZE_SR    0x1000      /* Blocksize for SRC */
#define MAXSIZE 0x00100000  /* Max. available convolver size of zita-convolver */

#define DB_CO(g) ((g) > -90.0f ? exp10f((g) * 0.05f) : 0.0f)
#define CO_DB(g) ((g) > 0.0f ? 20.0f * log10f(g) : -90.0f)

#define SMOOTH_CO_0      0.01
#define SMOOTH_CO_1      0.99

/* Audio I/O ports */
#define IR_PORT_INPUT_L       0
#define IR_PORT_INPUT_R       1
#define IR_PORT_OUTPUT_L      2
#define IR_PORT_OUTPUT_R      3

/* Control ports */
#define IR_PORT_REVERSE       4  /* Reverse impulse response [on/off] */
#define IR_PORT_PREDELAY      5  /* Predelay [ms] */
#define IR_PORT_ATTACK        6  /* Attack [%] */
#define IR_PORT_ATTACKTIME    7  /* Attack time [ms] */
#define IR_PORT_ENVELOPE      8  /* Envelope [%] */
#define IR_PORT_LENGTH        9  /* Length [%] */
#define IR_PORT_STRETCH      10  /* Stretch [%] */
#define IR_PORT_STEREO_IN    11  /* Stereo width In [%] */
#define IR_PORT_STEREO_IR    12  /* Stereo width IR [%] */
#define IR_PORT_AGC_SW       13  /* Autogain switch [on/off] */
#define IR_PORT_DRY_SW       14  /* Dry switch [on/off] */
#define IR_PORT_DRY_GAIN     15  /* Dry gain [dB] */
#define IR_PORT_WET_SW       16  /* Wet switch [on/off] */
#define IR_PORT_WET_GAIN     17  /* Wet gain [dB] */

/* Save/Restore ports */
#define IR_PORT_FHASH_0      18
#define IR_PORT_FHASH_1      19
#define IR_PORT_FHASH_2      20

/* Meter ports (output) */
#define IR_PORT_METER_DRY_L  21
#define IR_PORT_METER_DRY_R  22
#define IR_PORT_METER_WET_L  23
#define IR_PORT_METER_WET_R  24

typedef struct _ir {
	/* LV2 extensions */
	/*
	LV2_URI_Map_Callback_Data uri_callback_data;
	uint32_t (*uri_to_id)(LV2_URI_Map_Callback_Data callback_data,
	                      const char * map, const char * uri);
	*/

	/* Audio I/O ports */
	const float * in_L;
	const float * in_R;
	float * out_L;
	float * out_R;

	/* Control ports */
	float * port_reverse;
	float * port_predelay;
	float * port_attack;
	float * port_attacktime;
	float * port_envelope;
	float * port_length;
	float * port_stretch;
	float * port_stereo_in;
	float * port_stereo_ir;
	float * port_agc_sw;
	float * port_dry_sw;
	float * port_dry_gain;
	float * port_wet_sw;
	float * port_wet_gain;
	float * port_fhash_0;
	float * port_fhash_1;
	float * port_fhash_2;
	float * port_meter_dry_L;
	float * port_meter_dry_R;
	float * port_meter_wet_L;
	float * port_meter_wet_R;

	/* Thread that loads and computes configurations */
	GThread * conf_thread;
	int conf_thread_exit;
	int first_conf_done;

	/* Run notify flag */
	int run; /* plugin sets this to 1 in each run(),
		    UI or conf_thread may set it to 0 and watch */

	/* Configuration state */
	char * source_path;  /* path of IR audio file */
	SNDFILE * Finp;
	SF_INFO Sinp;
	uint32_t source_samplerate;
	int nchan; /* valid values are 1, 2, 4 */
	int source_nfram; /* length of source_samples */
	float * source_samples; /* IR audio file loads into this array INTERLEAVED */
	int ir_nfram; /* length of resampled & ir_samples */
	float * resampled_samples; /* Resampled IR samples INTERLEAVED */
	float ** ir_samples;     /* de-interleaved, processed samples loaded into Conv */
	float autogain;      /* dB */
	float autogain_new;  /* dB */

	float src_progress;
	SRC_STATE * src_state;
	SRC_DATA src_data;
	int src_in_frames;
	int src_out_frames;

	/* Processing state */
	float width;     /*  internal  */
	float dry_gain;  /* (smoothed) */
	float wet_gain;  /* parameters */

	double sample_rate;
	uint32_t maxsize; /* maximum size of IR supported by Convproc instance */
	uint32_t block_length; /* defaults to 1024, but may change(?) */

	Convproc * conv_0; /* zita-convolver engine class instances */
	Convproc * conv_1;
	int conv_in_use;
	int conv_req_to_use;
	int resample_pending;
	int reinit_pending;
	int reinit_running;

	/* These reference IR lib globals so GUI has access to them */
	GKeyFile * keyfile;
	GtkListStore * store_bookmarks;

	/* Function pointers for GUI */
	int (*load_sndfile)(struct _ir *);
	int (*resample_init)(struct _ir *);
	int (*resample_do)(struct _ir *);
	void (*resample_cleanup)(struct _ir *);
	void (*prepare_convdata)(struct _ir *);
	void (*init_conv)(struct _ir *);

	/* IR -> UI cleanup notification function */
	void (*cleanup_notify)(void * ui_handle);
	void * ui_handle; /* opaque ref. to a struct control in UI */
} IR;

#endif /* _IR_H */
