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

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "ir.h"
#include "ir_utils.h"

/* Hash function to obtain key to IR file path.
 * This is the djb2 algorithm taken from:
 *   http://www.cse.yorku.ca/~oz/hash.html
 */
uint64_t fhash(char *str) {
        uint64_t hash = 5381;
        int c;
	
        while ((c = *str++)) {
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	}
        return hash;
}

uint64_t fhash_from_ports(float *port0, float *port1, float *port2) {
	uint64_t val0 = ((uint64_t)*port0) & 0xffff;
	uint64_t val1 = ((uint64_t)*port1) & 0xffffff;
	uint64_t val2 = ((uint64_t)*port2) & 0xffffff;
	return (val0 << 48) + (val1 << 24) + val2;
}

void ports_from_fhash(uint64_t fhash, float *port0, float *port1, float *port2) {
	*port0 = (float)((fhash >> 48) & 0xffff);
	*port1 = (float)((fhash >> 24) & 0xffffff);
	*port2 = (float)(fhash & 0xffffff);
}

GKeyFile * load_keyfile(void) {
	GKeyFile * keyfile = g_key_file_new();
	gchar * ir_save_path = g_build_filename(g_get_home_dir(), IR_SAVE_FILE, NULL);
	if (g_file_test(ir_save_path, G_FILE_TEST_EXISTS) &&
	    g_file_test(ir_save_path, G_FILE_TEST_IS_REGULAR)) {
		if (!g_key_file_load_from_file(keyfile, ir_save_path,
					       G_KEY_FILE_NONE, NULL)) {
			fprintf(stderr, "IR: could not load configuration data from %s\n",
				ir_save_path);
		}
	}
	g_free(ir_save_path);
	return keyfile;
}

void save_keyfile(GKeyFile * keyfile) {
	gchar * ir_save_path = g_build_filename(g_get_home_dir(), IR_SAVE_FILE, NULL);
	gchar * file_contents = g_key_file_to_data(keyfile, NULL, NULL);
	if (!g_file_set_contents(ir_save_path, file_contents, -1, NULL)) {
		fprintf(stderr, "IR: error saving configuration data to %s\n", ir_save_path);
	}
	g_free(ir_save_path);
	g_free(file_contents);
}

char * get_path_from_key(GKeyFile * keyfile, uint64_t fhash) {
	char key[20];
	snprintf(key, 20, "%016" PRIx64, fhash);
	char * path = g_key_file_get_string(keyfile, GROUP_FHASH, key, NULL);
	return path;
}

void save_path(GKeyFile * keyfile, char * path) {
	uint64_t hash = fhash(path);
	char key[20];
	snprintf(key, 20, "%016" PRIx64, hash);
	g_key_file_set_string(keyfile, GROUP_FHASH, key, path);
}

void load_bookmarks(GKeyFile * keyfile, GtkListStore * store) {
	gchar ** keys = g_key_file_get_keys(keyfile, GROUP_BOOKMARKS, NULL, NULL);
	gchar ** k = keys;
	while (k && *k) {
		gchar * str = g_key_file_get_string(keyfile, GROUP_BOOKMARKS, *k, NULL);
		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, *k, 1, str, -1);
		free(str);
		++k;
	}
	g_strfreev(keys);
}

/* returns path if found, NULL if not found */
char * lookup_bookmark_in_store(GtkTreeModel * model, const char * bookmark) {
	GtkTreeIter iter;
	if (!gtk_tree_model_get_iter_first(model, &iter)) {
		return NULL;
	}
	do {
		char * key;
		char * path;
		gtk_tree_model_get(model, &iter, 0, &key, 1, &path, -1);
		if (strcmp(key, bookmark) == 0) {
			g_free(key);
			return path;
		} else {
			g_free(key);
			g_free(path);
		}
	} while (gtk_tree_model_iter_next(model, &iter));
	return NULL;
}

void store_bookmark(GKeyFile * keyfile, char * bookmark, char * fullpath) {
	g_key_file_set_string(keyfile, GROUP_BOOKMARKS, bookmark, fullpath);
}

void delete_bookmark(GKeyFile * keyfile, char * bookmark) {
	g_key_file_remove_key(keyfile, GROUP_BOOKMARKS, bookmark, NULL);
}

int filename_filter(const char * file) {
	if (!file) { return 0; }
	if (strlen(file) < 5) { return 0; }
	const char * ext = file + strlen(file)-4;
	if (strcmp(ext, ".wav") == 0) {	return 1; }
	if (strcmp(ext, ".WAV") == 0) {	return 1; }
	if (strcmp(ext, ".aiff") == 0) { return 1; }
	if (strcmp(ext, ".AIFF") == 0) { return 1; }
	if (strcmp(ext, ".au") == 0) { return 1; }
	if (strcmp(ext, ".AU") == 0) { return 1; }
	if (strcmp(ext, ".flac") == 0) { return 1; }
	if (strcmp(ext, ".FLAC") == 0) { return 1; }
	if (strcmp(ext, ".ogg") == 0) { return 1; }
	if (strcmp(ext, ".OGG") == 0) { return 1; }
	return 0;
}

int dirname_filter(const char * file) {
	if (!file) { return 0; }
	if (strlen(file) < 1) { return 0; }
	if (file[0] == '.') { return 0; }
	return 1;
}

void load_files(GtkListStore * store, char * dirpath) {
	gtk_list_store_clear(store);
	GDir * dir = g_dir_open(dirpath, 0, NULL);
	if (!dir) {
		return;
	}
	const char * file;
	while ((file = g_dir_read_name(dir))) {
		char * filepath = g_build_filename(dirpath, file, NULL);
		if ((g_file_test(filepath, G_FILE_TEST_IS_DIR) &&
		     dirname_filter(file)) ||
		    filename_filter(file)) {
			GtkTreeIter iter;
			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, 0, file, 1, filepath, -1);
		}
		g_free(filepath);
	}
	g_dir_close(dir);
}

void select_entry(GtkTreeModel * model, GtkTreeSelection * select, char * filename) {
	GtkTreeIter iter;
	if (!gtk_tree_model_get_iter_first(model, &iter)) {
		return;
	}
	do {
		char * path;
		gtk_tree_model_get(model, &iter, 1, &path, -1);
		if (strcmp(filename, path) == 0) {
			gtk_tree_selection_select_iter(select, &iter);
			g_free(path);
			return;
		}
	} while (gtk_tree_model_iter_next(model, &iter));
	gtk_tree_selection_unselect_all(select);
}

#define TAU 0.25
/* attack & envelope curves where t = 0..1 */
#define ATTACK_FN(t) (exp(((t) - 1.0) / TAU))
#define ENVELOPE_FN(t) (exp(-1.0 * (t) / TAU))

void compute_envelope(float ** samples, int nchan, int nfram,
		      int attack_time_s, float attack_pc,
		      float env_pc, float length_pc) {
	float gain;

	if (attack_time_s > nfram) {
		attack_time_s = nfram;
	}

	for (int j = 0; j < attack_time_s; j++) {
		gain = attack_pc / 100.0 +
			(100.0 - attack_pc) / 100.0 *
			ATTACK_FN((double)j / attack_time_s);
		for (int i = 0; i < nchan; i++) {
			samples[i][j] *= gain;
		}
	}

	int length_s = length_pc / 100.0 * (nfram - attack_time_s);
	for (int j = attack_time_s, k = 0; j < attack_time_s + length_s; j++, k++) {
		gain = env_pc / 100.0 +
			(100.0 - env_pc) / 100.0 *
			ENVELOPE_FN((double)k / length_s);
		for (int i = 0; i < nchan; i++) {
			samples[i][j] *= gain;
		}
	}

	for (int j = attack_time_s + length_s; j < nfram; j++) {
		for (int i = 0; i < nchan; i++) {
			samples[i][j] = 0.0f;
		}
	}
}

void draw_centered_text(cairo_t * cr, const char * text, int x , int y) {
	cairo_text_extents_t extents;
	cairo_text_extents(cr, text, &extents);
	int text_x = x - (extents.width/2 + extents.x_bearing);
	int text_y = y -(extents.height/2 + extents.y_bearing);
	cairo_move_to(cr, text_x, text_y);
	cairo_show_text(cr, text);
}
