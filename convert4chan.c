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

/* convert4chan utility
 *
 * Convert pairs of stereo files named *[LR].wav to four-channel files
 * named *4.wav. Recursively convert all suitable files under current
 * working directory, or full directory path supplied as first
 * argument.
 *
 * Compile with: 'make convert4chan'
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <sndfile.h>

#define BUFLEN 4096

float bufL[2*BUFLEN];
float bufR[2*BUFLEN];
float buf4[4*BUFLEN];

/* exclude hidden dirs */
int dirname_filter(const char * file) {
	if (!file) { return 0; }
	if (strlen(file) < 1) { return 0; }
	if (file[0] == '.') { return 0; }
	return 1;
}

int filename_filter(const char * file) {
	if (!file) { return 0; }
	if (strlen(file) < 6) { return 0; }
	const char * ext = file + strlen(file)-5;
	if (strcmp(ext, "L.wav") == 0) { return 1; }
	if (strcmp(ext, "L.WAV") == 0) { return 1; }
	return 0;
}

void really_convert(char * file_L, char * file_R, char * file_4) {

	SNDFILE * FL;
	SNDFILE * FR;
	SNDFILE * F4;
	SF_INFO SL;
	SF_INFO SR;
	SF_INFO S4;
	int length;

	FL = sf_open(file_L, SFM_READ, &SL);
	if (!FL) {
		fprintf(stderr, "*** unable to read input file %s\n", file_L);
		return;
	}
	FR = sf_open(file_R, SFM_READ, &SR);
	if (!FR) {
		fprintf(stderr, "*** unable to read input file %s\n", file_R);
		goto close_FL;
	}

	length = SL.frames;
	if (SR.frames > length) {
		length = SR.frames;
	}

	if (SL.samplerate != SR.samplerate) {
		fprintf(stderr, "*** source samplerates differ!\n");
		goto close_FR;
	}
	if (SL.format != SR.format) {
		printf("warning: sample formats differ, using left file's format for output\n");
	}

	if (SL.channels != 2) {
		fprintf(stderr, "*** source L is not stereo!\n");
		goto close_FR;
	}
	if (SR.channels != 2) {
		fprintf(stderr, "*** source R is not stereo!\n");
		goto close_FR;
	}

	S4.channels = 4;
	S4.samplerate = SL.samplerate;
	S4.format = SL.format;
	F4 = sf_open(file_4, SFM_WRITE, &S4);
	if (!F4) {
		fprintf(stderr, "*** unable to write file %s\n", file_4);
		goto close_FR;
	}
	
	int readL = 1;
	int readR = 1;
	while (length && (readL || readR)) {
		int n = (length > BUFLEN) ? BUFLEN : length;
		int n1, n2;
		memset(bufL, 0, sizeof(bufL));
		memset(bufR, 0, sizeof(bufR));
		memset(buf4, 0, sizeof(buf4));

		if (readL) {
			n1 = sf_readf_float(FL, bufL, n);
			if (n1 < 0) {
				fprintf(stderr, "error reading file %s\n", file_L);
				goto close;
			} else if (n1 == 0) {
				readL = 0;
			}
		} else {
			n1 = 0;
		}

		if (readR) {
			n2 = sf_readf_float(FR, bufR, n);
			if (n2 < 0) {
				fprintf(stderr, "error reading file %s\n", file_R);
				goto close;
			} else if (n2 == 0) {
				readR = 0;
			}
		} else {
			n2 = 0;
		}

		int min = n1;
		if (n2 < min) {
			min = n2;
		}
		int i;
		for (i = 0; i < min; i++) {
			buf4[4*i]     = bufL[2*i];
			buf4[4*i + 1] = bufL[2*i + 1];
			buf4[4*i + 2] = bufR[2*i];
			buf4[4*i + 3] = bufR[2*i + 1];
		}
		if (n1 > min) {
			for (; i < n1; i++) {
				buf4[4*i]     = bufL[2*i];
				buf4[4*i + 1] = bufL[2*i + 1];
				buf4[4*i + 2] = 0;
				buf4[4*i + 3] = 0;
			}
		} else {
			for (; i < n2; i++) {
				buf4[4*i]     = 0;
				buf4[4*i + 1] = 0;
				buf4[4*i + 2] = bufR[2*i];
				buf4[4*i + 3] = bufR[2*i + 1];
			}
		}
		length -= i;

		int nw = sf_writef_float(F4, buf4, i);
		if (nw < 0) {
			fprintf(stderr, "error writing file %s\n", file_4);
			goto close;
		}
	}

 close:
	sf_close(F4);
 close_FR:
	sf_close(FR);
 close_FL:
	sf_close(FL);
}

void convert_files(char * file_L) {

	char * file_R = strdup(file_L);
	char * file_4;
	file_R[strlen(file_R)-5] = 'R';

	printf("converting pair: %s\n", file_L);
	printf("                 %s\n", file_R);

	if (!g_file_test(file_R, G_FILE_TEST_IS_REGULAR)) {
		fprintf(stderr, "*** %s is not a regular file, skipping pair\n", file_R);
		free(file_R);
		return;
	}

	file_4 = strdup(file_L);
	file_4[strlen(file_4)-5] = '4';
	printf("      to output: %s\n", file_4);

	really_convert(file_L, file_R, file_4);
	printf("\n");

	free(file_R);
	free(file_4);
}

void iterate_over_dir(char * dirname) {
	GDir * dir = g_dir_open(dirname, 0, NULL);
	if (!dir) {
		free(dirname);
		return;
	}
	const char * file;
	while ((file = g_dir_read_name(dir))) {
		char * filepath = g_build_filename(dirname, file, NULL);
		if (g_file_test(filepath, G_FILE_TEST_IS_DIR) &&
		    dirname_filter(file)) {
			iterate_over_dir(filepath);
		} else if ((g_file_test(filepath, G_FILE_TEST_IS_REGULAR)) &&
			   filename_filter(file)) {
			convert_files(filepath);
		}
		g_free(filepath);
	}
	g_dir_close(dir);
}

int main(int argc, char ** argv) {

	char * dirname;
	if (argc > 1) {
		dirname = strdup(argv[1]);
	} else {
		dirname = g_get_current_dir();
	}
	iterate_over_dir(dirname);
	free(dirname);
	return 0;
}
