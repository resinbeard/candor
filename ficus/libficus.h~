
/* libficus.h
This file is a part of 'ficus'
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

'libficus' is a really simple api for playback/recording of WAV audio data implemented in C 
Copyright 2013 murray foster 
mrafoster at gmail dawtcom */

#ifndef libficus_h__
#define libficus_h__

int ficus_setup(char *client_name, char *path, char *prefix, int bit_depth);

int ficus_loadfile(char *path, int bank_number);

int ficus_loop(int bank_number, int state);

int ficus_setmixout(int bank_number, int channel, int state);
int ficus_setmixin(int bank_number, int channel, int state);

int ficus_jackmonitor(int channel_out, int channel_in, int state);

void ficus_playback(int bank_number);
void ficus_playback_speed(int bank_number, float speed);

void ficus_playback_rampup(int bank_number, float rampduration);
void ficus_playback_rampdown(int bank_number, float rampduration);

int ficus_capture(int bank_number, int seconds);
int ficus_capturef(int bank_number, int num_frames);
int ficus_durationf(int bank_number);

int ficus_killplayback(int bank_number);
int ficus_killcapture(int bank_number);

int ficus_isplaying(int bank_number);
int ficus_iscapturing(int bank_number);

int ficus_islooping(int bank_number);

void ficus_clean();

#endif
