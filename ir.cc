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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <sndfile.h>
#include <samplerate.h>
#include <zita-convolver.h>
#include <lv2.h>

#include "ir.h"
#include "ir_utils.h"

#define ZITA_CONVOLVER_VERSION  0
#if ZITA_CONVOLVER_MAJOR_VERSION == 3
#undef ZITA_CONVOLVER_VERSION
#define ZITA_CONVOLVER_VERSION  3
#elif ZITA_CONVOLVER_MAJOR_VERSION == 4
#undef ZITA_CONVOLVER_VERSION
#define ZITA_CONVOLVER_VERSION  4
#else
#error "This version of IR requires zita-convolver 3.x.x or 4.x.x"
#endif

/* You may need to change these to match your JACK server setup!
 *
 * Priority should match -P parameter passed to jackd.
 * Sched.class: either SCHED_FIFO or SCHED_RR (I think Jack uses SCHED_FIFO).
 *
 * THREAD_SYNC_MODE must be true if you want to use the plugin in Jack
 * freewheeling mode (eg. while exporting in Ardour). You may only use
 * false if you *only* run the plugin realtime.
 */
#define CONVPROC_SCHEDULER_PRIORITY 0
#define CONVPROC_SCHEDULER_CLASS SCHED_FIFO
#define THREAD_SYNC_MODE true


static LV2_Descriptor * IR_Descriptor = NULL;
static GKeyFile * keyfile = NULL;
static GtkListStore * store_bookmarks = NULL;
G_LOCK_DEFINE_STATIC(conv_configure_lock);

static void connectPortIR(LV2_Handle instance,
			  uint32_t port,
			  void * data) {

	IR * ir = (IR *)instance;

	switch (port) {
	/* Audio I/O */
	case IR_PORT_INPUT_L:
		ir->in_L = (const float*)data;
		break;
	case IR_PORT_INPUT_R:
		ir->in_R = (const float*)data;
		break;
	case IR_PORT_OUTPUT_L:
		ir->out_L = (float*)data;
		break;
	case IR_PORT_OUTPUT_R:
		ir->out_R = (float*)data;
		break;

	/* Control */
	case IR_PORT_REVERSE:
		ir->port_reverse = (float*)data;
		break;
	case IR_PORT_PREDELAY:
		ir->port_predelay = (float*)data;
		break;
	case IR_PORT_ATTACK:
		ir->port_attack = (float*)data;
		break;
	case IR_PORT_ATTACKTIME:
		ir->port_attacktime = (float*)data;
		break;
	case IR_PORT_ENVELOPE:
		ir->port_envelope = (float*)data;
		break;
	case IR_PORT_LENGTH:
		ir->port_length = (float*)data;
		break;
	case IR_PORT_STRETCH:
		ir->port_stretch = (float*)data;
		break;
	case IR_PORT_STEREO_IN:
		ir->port_stereo_in = (float*)data;
		break;
	case IR_PORT_STEREO_IR:
		ir->port_stereo_ir = (float*)data;
		break;
	case IR_PORT_AGC_SW:
		ir->port_agc_sw = (float*)data;
		break;
	case IR_PORT_DRY_SW:
		ir->port_dry_sw = (float*)data;
		break;
	case IR_PORT_DRY_GAIN:
		ir->port_dry_gain = (float*)data;
		break;
	case IR_PORT_WET_SW:
		ir->port_wet_sw = (float*)data;
		break;
	case IR_PORT_WET_GAIN:
		ir->port_wet_gain = (float*)data;
		break;

	/* Save/Restore */
	case IR_PORT_FHASH_0:
		ir->port_fhash_0 = (float*)data;
		break;
	case IR_PORT_FHASH_1:
		ir->port_fhash_1 = (float*)data;
		break;
	case IR_PORT_FHASH_2:
		ir->port_fhash_2 = (float*)data;
		break;

	/* Meter ports */
	case IR_PORT_METER_DRY_L:
		ir->port_meter_dry_L = (float*)data;
		break;
	case IR_PORT_METER_DRY_R:
		ir->port_meter_dry_R = (float*)data;
		break;
	case IR_PORT_METER_WET_L:
		ir->port_meter_wet_L = (float*)data;
		break;
	case IR_PORT_METER_WET_R:
		ir->port_meter_wet_R = (float*)data;
		break;
	}
}

void free_ir_samples(IR * ir) {
	if (ir->ir_samples != 0) {
		float **p = ir->ir_samples;
		while (*p) {
			free(*p++);
		}
		free(ir->ir_samples);
		ir->ir_samples = 0;
	}
}

void free_conv_safely(Convproc * conv) {
	unsigned int state;

	if (!conv) {
		return;
	}
	state = conv->state();
	if (state != Convproc::ST_STOP) {
		conv->stop_process();
	}
	conv->cleanup();
	delete conv;	
}

void free_convproc(IR * ir) {
	free_conv_safely(ir->conv_0);
	ir->conv_0 = 0;
	free_conv_safely(ir->conv_1);
	ir->conv_1 = 0;
}

static void cleanupIR(LV2_Handle instance) {
	IR * ir = (IR*)instance;

	if (!ir->first_conf_done) {
		ir->conf_thread_exit = 1;
		g_thread_join(ir->conf_thread);
	}

	free_convproc(ir);
	if (ir->source_samples != NULL) {
		free(ir->source_samples);
		ir->source_samples = NULL;
	}
	if (ir->resampled_samples != NULL) {
		free(ir->resampled_samples);
		ir->resampled_samples = NULL;
	}
	free_ir_samples(ir);

	if (ir->source_path && (strlen(ir->source_path) > 0)) {
		save_path(keyfile, ir->source_path);
		free(ir->source_path);
	}
	free(instance);
}

/* Read IR audio file
 *   input data: source_path
 *  output data: nchan, source_samples
 * return value: 0 OK, < 0 error
 */
static int load_sndfile(IR * ir) {

	int length = 0;
	int offset = 0;
	float * buff;

	if (!(ir->source_path) || *ir->source_path != '/') {
		fprintf(stderr, "IR: load_sndfile error: %s is not an absolute path\n",
			ir->source_path);
		return -1;
	}
	
	ir->Finp = sf_open(ir->source_path, SFM_READ, &ir->Sinp);
	if (!ir->Finp) {
		fprintf(stderr, "IR: unable to read IR input file '%s'\n",
			ir->source_path);
		return -1;
	}

	ir->source_samplerate = ir->Sinp.samplerate;
	ir->nchan = ir->Sinp.channels;
	ir->source_nfram = ir->Sinp.frames;

	if ((ir->nchan != 1) && (ir->nchan != 2) && (ir->nchan != 4)) {
		fprintf(stderr, "IR: channel count %d of '%s' not supported.\n",
			ir->nchan, ir->source_path);
		sf_close(ir->Finp);
		return -1;
	}

	length = ir->source_nfram;
	if (ir->source_samples != NULL) {
		free(ir->source_samples);
	}
	ir->source_samples = (float*)malloc(ir->nchan * length * sizeof(float));
        buff = new float[BSIZE * ir->nchan];

	while (length) {
		int n = (length > BSIZE) ? BSIZE : length;
		n = sf_readf_float(ir->Finp, buff, n);
		if (n < 0) {
			fprintf(stderr, "IR: error reading file %s\n", ir->source_path);
			sf_close(ir->Finp);
			delete[] buff;
			return -1;
		}
		if (n) {
			for (int i = 0; i < n * ir->nchan; i++) {
				ir->source_samples[offset + i] = buff[i];
			}
			offset += n * ir->nchan;
			length -= n;
		}
	}

	delete[] buff;
	sf_close(ir->Finp);

	return 0;
}

/* Resample the IR samples, taking stretch into account
 *    input: source_nfram, source_samples
 *   output: ir_nfram, resampled_samples
 * This function sets up the resampling operation
 * return: 0: OK, 1: OK, no SRC needed, -1: error
 */
int resample_init(IR * ir) {

	float stretch = *ir->port_stretch / 100.0;
	float fs_out = ir->sample_rate * stretch;

	if (!ir->source_samples || !ir->source_nfram || !ir->nchan) {
		return -1;
	}

	if (ir->source_samplerate == (unsigned int)fs_out) {
		ir->ir_nfram = ir->source_nfram;
		if (ir->resampled_samples != NULL) {
		        free(ir->resampled_samples);
		}
		ir->resampled_samples =
		        (float*)calloc(ir->nchan * ir->ir_nfram, sizeof(float));
		for (int i = 0; i < ir->nchan * ir->ir_nfram; i++) {
		        ir->resampled_samples[i] = ir->source_samples[i];
		}
		return 1;
	}

	ir->ir_nfram = ir->source_nfram * fs_out / ir->source_samplerate + 1;

	//printf("IR Resampler: fs_in=%d fs_out=%f\n", ir->source_samplerate, fs_out);
	//printf("              samples_in=%d samples_out=%d\n", ir->source_nfram, ir->ir_nfram);

	if (ir->resampled_samples != NULL) {
		free(ir->resampled_samples);
	}
	ir->resampled_samples = (float*)calloc(ir->nchan * ir->ir_nfram, sizeof(float));

	int src_error;
	ir->src_state = src_new(SRC_SINC_BEST_QUALITY, ir->nchan, &src_error);
	if (ir->src_state == NULL) {
		fprintf(stderr, "IR: src_new() error: %s\n", src_strerror(src_error));
		return -1;
	}
	src_error = src_set_ratio(ir->src_state, fs_out / ir->source_samplerate);
	if (src_error) {
		fprintf(stderr, "IR: src_set_ratio() error: %s, new_ratio = %g\n",
			src_strerror(src_error), fs_out / ir->source_samplerate);
		src_delete(ir->src_state);
		return -1;
	}

	ir->src_progress = 0.0;
	ir->src_in_frames = ir->source_nfram;
	ir->src_out_frames = 0;
	ir->src_data.data_in = ir->source_samples;
	ir->src_data.data_out = ir->resampled_samples;
	ir->src_data.input_frames_used = 0;
	ir->src_data.output_frames_gen = 0;
	ir->src_data.src_ratio = fs_out / ir->source_samplerate; /* really needed? */
	ir->src_data.end_of_input = 0;
	return 0;
}

/* Do a chunk of resample processing
 * return: 0: OK, not ready (call it again); 1: ready; -1: error
 * ir->src_progress can be used to track progress of resampling
 */
int resample_do(IR * ir) {
	if (!ir->src_in_frames) {
		return 1;
	}

	ir->src_data.input_frames = (ir->src_in_frames > BSIZE_SR) ? BSIZE_SR : ir->src_in_frames;
	ir->src_data.output_frames = ir->ir_nfram - ir->src_out_frames;
	
	//printf("src_progress %f\n", ir->src_progress);
	int src_error = src_process(ir->src_state, &ir->src_data);
	if (src_error != 0) {
		fprintf(stderr, "IR: src_process() error: %s\n", src_strerror(src_error));
		src_delete(ir->src_state);
		return -1;
	}

	ir->src_data.data_in += ir->nchan * ir->src_data.input_frames_used;
	ir->src_data.data_out += ir->nchan * ir->src_data.output_frames_gen;
	ir->src_in_frames -= ir->src_data.input_frames_used;
	ir->src_out_frames += ir->src_data.output_frames_gen;
	ir->src_progress = (float)ir->src_out_frames / ir->ir_nfram;

	return ir->src_in_frames ? 0 : 1;
}

/* Finish resampling; call this after resample_do returned 1 */
void resample_cleanup(IR * ir) {
	if (ir->src_out_frames < ir->ir_nfram) {
		ir->ir_nfram = ir->src_out_frames;
	}
	ir->src_progress = 1.0;
	src_delete(ir->src_state);
}

/* In place processing on ir_samples[] */
void process_envelopes(IR * ir) {

	int attack_time_s = (int)*ir->port_attacktime * ir->sample_rate / 1000;
	float attack_pc = *ir->port_attack;
	float length_pc = *ir->port_length;
	float env_pc = *ir->port_envelope;
	
	compute_envelope(ir->ir_samples, ir->nchan, ir->ir_nfram,
			 attack_time_s, attack_pc,
			 env_pc, length_pc);
}

/* Mid-Side based Stereo width effect */
void ms_stereo(float width, float * lp, float * rp, int length) {

	float w = width / 100.0f;
	float x = (1.0 - w) / (1.0 + w); /* M-S coeff.; L_out = L + x*R; R_out = x*L + R */
	float L, R;

	for (int i = 0; i < length; i++) {
		L = *lp;
		R = *rp;
		*lp++ = L + x * R;
		*rp++ = R + x * L;
	}
}

/* Prepare samples to be loaded into convolution engine
 *    input: ir_nfram, resampled_samples
 *   output: ir_samples
 *   parameters: all plugin parameters except stretch,
 *               stereo_in, dry_*, wet_*
 */
void prepare_convdata(IR * ir) {

	if (!ir->resampled_samples || !ir->ir_nfram || !ir->nchan) {
		return;
	}

	free_ir_samples(ir);
	ir->ir_samples = (float**)malloc((1 + ir->nchan) * sizeof(float*));
	for (int i = 0; i < ir->nchan; i++) {
		ir->ir_samples[i] = (float*)malloc(ir->ir_nfram * sizeof(float));
	}
	ir->ir_samples[ir->nchan] = NULL;

	/* de-interleave resampled_samples to ir_samples */
	for (int ch = 0; ch < ir->nchan; ch++) {
		float * p = ir->resampled_samples + ch;
		float * q = ir->ir_samples[ch];
		int nch = ir->nchan;
		int nfram = ir->ir_nfram;
		for (int i = 0; i < nfram; i++) {
			q[i] = p[i * nch];
		}
	}

	/* Autogain calculation */
	float pow = 0;
	for (int ch = 0; ch < ir->nchan; ch++) {
		float * p = ir->ir_samples[ch];
		for (int i = 0; i < ir->ir_nfram; i++) {
			pow += p[i] * p[i];
		}
	}
	pow /= ir->nchan;
	ir->autogain_new = -10.0 * log10f(pow / 6.0);

	/* IR stereo width */
	if (ir->nchan == 2) {
		ms_stereo(*ir->port_stereo_ir, ir->ir_samples[0], ir->ir_samples[1], ir->ir_nfram);
	} else if (ir->nchan == 4) {
		ms_stereo(*ir->port_stereo_ir, ir->ir_samples[0], ir->ir_samples[1], ir->ir_nfram);
		ms_stereo(*ir->port_stereo_ir, ir->ir_samples[2], ir->ir_samples[3], ir->ir_nfram);
	}
	process_envelopes(ir);

	/* reverse ir vector if needed */
	int reverse = (*ir->port_reverse > 0.0f) ? 1 : 0;
	if (reverse) {
		float tmp;
		int nfram = ir->ir_nfram;
		for (int ch = 0; ch < ir->nchan; ch++) {
			float * p = ir->ir_samples[ch];
			for (int i = 0, j = nfram-1; i < nfram/2; i++, j--) {
				tmp = p[i];
				p[i] = p[j];
				p[j] = tmp;
			}
		}
	}
}

/* Initialise (the next) convolution engine to use */
void init_conv(IR * ir) {

	Convproc * conv;
	int req_to_use;

	if (!ir->ir_samples || !ir->ir_nfram || !ir->nchan) {
		return;
	}

	if (ir->conv_in_use != ir->conv_req_to_use) {
		fprintf(stderr, "IR init_conv: error, engine still in use!\n");
		return;
	}

	if (ir->conv_in_use == 1) { /* new one will be 0th */
		free_conv_safely(ir->conv_0);
		ir->conv_0 = new Convproc;
		conv = ir->conv_0;
		req_to_use = 0;
	} else { /* new one will be 1st */
		free_conv_safely(ir->conv_1);
		ir->conv_1 = new Convproc;
		conv = ir->conv_1;
		req_to_use = 1;
	}

	uint32_t predelay_samples = (int)*ir->port_predelay * ir->sample_rate / 1000;
	uint32_t length = ir->maxsize;
	int nfram;

	if (predelay_samples + ir->ir_nfram > length) {
		fprintf(stderr, "IR: warning: truncated IR to %d samples\n", length);
		nfram = length - predelay_samples;
	} else {
		nfram = ir->ir_nfram;
		length = predelay_samples + ir->ir_nfram;
	}

	if (length < ir->block_length) {
		length = ir->block_length;
	}

	G_LOCK(conv_configure_lock);
	//printf("configure length=%d ir->block_length=%d\n", length, ir->block_length);
#if ZITA_CONVOLVER_VERSION == 3
	if (ir->nchan == 4) {
		conv->set_density(1);
	}
	int ret = conv->configure(2, // n_inputs
				  2, // n_outputs
				  length,
				  ir->block_length,
				  ir->block_length,
				  Convproc::MAXPART);
#elif ZITA_CONVOLVER_VERSION == 4
	float density = 0.0f;
	if (ir->nchan == 4) {
		density = 1.0f;
	}
	int ret = conv->configure(2, // n_inputs
				  2, // n_outputs
				  length,
				  ir->block_length,
				  ir->block_length,
				  Convproc::MAXPART,
				  density);
#endif
	G_UNLOCK(conv_configure_lock);
	if (ret != 0) {
		fprintf(stderr, "IR: can't initialise zita-convolver engine, Convproc::configure returned %d\n", ret);
		free_conv_safely(conv);
		if (req_to_use == 0) {
			ir->conv_0 = NULL;
		} else {
			ir->conv_1 = NULL;
		}
		return;
	}
	
	switch (ir->nchan) {
	case 1: /* both input channels are convolved with the same IR */
		conv->impdata_create(0, 0, 1, ir->ir_samples[0],
				     predelay_samples, nfram + predelay_samples);
		conv->impdata_copy(0, 0, 1, 1);
		break;
	case 2: /* left channel convolved with left IR channel yields left output
		   same for the right channel */
		conv->impdata_create(0, 0, 1, ir->ir_samples[0],
				     predelay_samples, nfram + predelay_samples);
		conv->impdata_create(1, 1, 1, ir->ir_samples[1],
				     predelay_samples, nfram + predelay_samples);
		break;
	case 4: /* IR is a full matrix:  / in_L -> out_L   in_L -> out_R \
		                         \ in_R -> out_L   in_R -> out_R /  */
		conv->impdata_create(0, 0, 1, ir->ir_samples[0],
				     predelay_samples, nfram + predelay_samples);
		conv->impdata_create(0, 1, 1, ir->ir_samples[1],
				     predelay_samples, nfram + predelay_samples);
		conv->impdata_create(1, 0, 1, ir->ir_samples[2],
				     predelay_samples, nfram + predelay_samples);
		conv->impdata_create(1, 1, 1, ir->ir_samples[3],
				     predelay_samples, nfram + predelay_samples);
		break;
	default: printf("IR init_conv: error, impossible value: ir->nchan = %d\n",
			ir->nchan);
	}

	conv->start_process(CONVPROC_SCHEDULER_PRIORITY, CONVPROC_SCHEDULER_CLASS);
	ir->conv_req_to_use = req_to_use;
}

gpointer IR_configurator_thread(gpointer data) {

	IR * ir = (IR *)data;

	struct timespec treq;
	struct timespec trem;

	while (!ir->conf_thread_exit) {
		if (ir->run && !ir->first_conf_done) {
			uint64_t fhash = fhash_from_ports(ir->port_fhash_0,
							  ir->port_fhash_1,
							  ir->port_fhash_2);
			//printf("IR confthread: fhash = %016" PRIx64 "\n", fhash);
			if (fhash) {
				char * filename = get_path_from_key(keyfile, fhash);
				if (filename) {
					//printf("  load filename=%s\n", filename);
					ir->source_path = filename;
					if (load_sndfile(ir) == 0) {
						int r = resample_init(ir);
						if (r == 0) {
							while ((r == 0) && (!ir->conf_thread_exit)) {
								r = resample_do(ir);
							}
							resample_cleanup(ir);
						}
						if (r >= 0) {
							prepare_convdata(ir);
							init_conv(ir);
						}
					} else {
						free(ir->source_path);
						ir->source_path = NULL;
					}
				} else {
					fprintf(stderr, "IR: fhash=%016" PRIx64
						" was not found in DB\n", fhash);
				}
			}
			ir->first_conf_done = 1;
			return NULL;
		}

		/* sleep 100 ms before checking again */
		treq.tv_sec = 0;
		treq.tv_nsec = 100000000;
		nanosleep(&treq, &trem);
	}
	return NULL;
}

static LV2_Handle instantiateIR(const LV2_Descriptor *descriptor,
				double sample_rate,
				const char *path,
				const LV2_Feature *const *features) {

	IR * ir = (IR *)calloc(1, sizeof(IR));

	ir->sample_rate = sample_rate;
	ir->reinit_pending = 0;
	ir->maxsize = MAXSIZE;
	ir->block_length = 1024;

	ir->load_sndfile = load_sndfile;
	ir->resample_init = resample_init;
	ir->resample_do = resample_do;
	ir->resample_cleanup = resample_cleanup;
	ir->prepare_convdata = prepare_convdata;
	ir->init_conv = init_conv;

	ir->keyfile = keyfile;
	ir->store_bookmarks = store_bookmarks;

	ir->conf_thread = g_thread_new("IR_configurator_thread",
                                       IR_configurator_thread, (gpointer)ir);
	return (LV2_Handle)ir;
}

#define ACC_MAX(m,v) (((fabs(v))>(m))?fabs(v):(m))
static void runIR(LV2_Handle instance, uint32_t sample_count) {

	IR * ir = (IR *)instance;
	Convproc * conv;

	const float * const in_L = ir->in_L;
	const float * const in_R = ir->in_R;
	float * const out_L = ir->out_L;
	float * const out_R = ir->out_R;

	float dry_L_meter = 0.0;
	float dry_R_meter = 0.0;
	float wet_L_meter = 0.0;
	float wet_R_meter = 0.0;

	ir->run = 1;

	if (ir->conv_in_use != ir->conv_req_to_use) {
		/* call stop_process() on the conv being switched away from
		 * so it can be deallocated next time it is needed */
		Convproc * conv_from;
		if (ir->conv_in_use == 0) {
			conv_from = ir->conv_0;
		} else {
			conv_from = ir->conv_1;
		}
		if (conv_from) {
			conv_from->stop_process();
		}

		ir->conv_in_use = ir->conv_req_to_use;
		ir->autogain = ir->autogain_new;
		//printf("IR engine switching to conv_%d  autogain = %f\n", ir->conv_in_use, ir->autogain);
	}
	if (ir->conv_in_use == 0) {
		conv = ir->conv_0;
	} else {
		conv = ir->conv_1;
	}

	float agc_gain_raw = (*ir->port_agc_sw > 0.0f) ? DB_CO(ir->autogain) : 1.0f;

	float width_raw = *ir->port_stereo_in / 100.0f; /* stereo width */
	float dry_sw = (*ir->port_dry_sw > 0.0f) ? 1.0f : 0.0f;
	float wet_sw = (*ir->port_wet_sw > 0.0f) ? 1.0f : 0.0f;
	float dry_gain_raw = DB_CO(*ir->port_dry_gain) * dry_sw;
	float wet_gain_raw = DB_CO(*ir->port_wet_gain) * wet_sw * agc_gain_raw;

	float width = ir->width;
	float dry_gain = ir->dry_gain;
	float wet_gain = ir->wet_gain;

	if (sample_count != ir->block_length) {
		if ((sample_count == 1) || (sample_count == 2) || (sample_count == 4) || (sample_count == 8) ||
		    (sample_count == 16) || (sample_count == 32) || (sample_count == 64) || (sample_count == 128) ||
		    (sample_count == 256) || (sample_count == 512) || (sample_count == 1024) || (sample_count == 2048) ||
		    (sample_count == 4096) || (sample_count == 8192) || (sample_count == 16384) || (sample_count == 32768)) {
			/* block size seems valid - sometimes Ardour runs us with an
			   irregular block size (eg. when muting out the track) */
			ir->block_length = sample_count;
			ir->reinit_pending = 1;
			conv = 0;
		}
	}

	int n = sample_count;
	float * p, * q;
	float dry_L, dry_R, wet_L, wet_R;

	if (conv != 0) {
		p = conv->inpdata(0);
		q = conv->inpdata(1);
		float x;
		for (int j = 0; j < n; j++) {
			width = width * SMOOTH_CO_1 + width_raw * SMOOTH_CO_0;
			x = (1.0 - width) / (1.0 + width); /* M-S coeff. */
			p[j] = in_L[j] + x * in_R[j];
			q[j] = in_R[j] + x * in_L[j];
		}
		
		conv->process(THREAD_SYNC_MODE);

		p = conv->outdata(0);
		q = conv->outdata(1);
		for (int j = 0; j < n; j++) {
			dry_gain = SMOOTH_CO_1 * dry_gain + SMOOTH_CO_0 * dry_gain_raw;
			wet_gain = SMOOTH_CO_1 * wet_gain + SMOOTH_CO_0 * wet_gain_raw;
			dry_L = in_L[j] * dry_gain;
			dry_R = in_R[j] * dry_gain;
			wet_L = p[j] * wet_gain;
			wet_R = q[j] * wet_gain;
			dry_L_meter = ACC_MAX(dry_L_meter, dry_L);
			dry_R_meter = ACC_MAX(dry_R_meter, dry_R);
			wet_L_meter = ACC_MAX(wet_L_meter, wet_L);
			wet_R_meter = ACC_MAX(wet_R_meter, wet_R);
			out_L[j] = dry_L + wet_L;
			out_R[j] = dry_R + wet_R;
		}
	} else { /* convolution engine not available */
		for (int j = 0; j < n; j++) {
			dry_gain = SMOOTH_CO_1 * dry_gain + SMOOTH_CO_0 * dry_gain_raw;
			dry_L = in_L[j] * dry_gain;
			dry_R = in_R[j] * dry_gain;
			dry_L_meter = ACC_MAX(dry_L_meter, dry_L);
			dry_R_meter = ACC_MAX(dry_R_meter, dry_R);
			out_L[j] = dry_L;
			out_R[j] = dry_R;
		}
	}

	ir->width = width;
	ir->dry_gain = dry_gain;
	ir->wet_gain = wet_gain;

	*ir->port_meter_dry_L = dry_L_meter;
	*ir->port_meter_dry_R = dry_R_meter;
	*ir->port_meter_wet_L = wet_L_meter;
	*ir->port_meter_wet_R = wet_R_meter;
}

const void * extdata_IR(const char * uri) {
	return NULL;
}

void __attribute__ ((constructor)) init() {

	if (zita_convolver_major_version () != ZITA_CONVOLVER_MAJOR_VERSION) {
		fprintf(stderr, "IR: compile-time & runtime library versions of zita-convolver do not match!\n");
		IR_Descriptor = NULL;
		return;
	}

	if (!g_thread_supported()) {
		fprintf(stderr, "IR: This plugin requires a working GLib with GThread.\n");
		IR_Descriptor = NULL;
		return;
	}

	IR_Descriptor = (LV2_Descriptor *)malloc(sizeof(LV2_Descriptor));

	IR_Descriptor->URI = IR_URI;
	IR_Descriptor->instantiate = instantiateIR;
	IR_Descriptor->cleanup = cleanupIR;
	IR_Descriptor->activate = NULL;
	IR_Descriptor->deactivate = NULL;
	IR_Descriptor->connect_port = connectPortIR;
	IR_Descriptor->run = runIR;
	IR_Descriptor->extension_data = extdata_IR;

	keyfile = load_keyfile();
	store_bookmarks = gtk_list_store_new(2,
					     G_TYPE_STRING,  /* visible name (key) */
					     G_TYPE_STRING); /* full pathname (value) */
	load_bookmarks(keyfile, store_bookmarks);
}

void __attribute__ ((destructor)) fini() {
	save_keyfile(keyfile);
	g_key_file_free(keyfile);
	g_object_unref(store_bookmarks);
	free(IR_Descriptor);
}

LV2_SYMBOL_EXPORT
const LV2_Descriptor * lv2_descriptor(uint32_t index) {

	switch (index) {
	case 0:
		return IR_Descriptor;
	default:
		return NULL;
	}
}
