/* config.h
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

Copyright 2013 murray foster 
mrafoster at gmail dawtcom */

#ifndef config_h__
#define config_h__

/* COMPILE-TIME OPTIONS */
#define NUM_SAMPLES 48 /* number of sound files to possibly store */

#define NUM_CHANNELS 2  /* number of possible in/out channels */
/*  1999999 */
#define IN_FRAMES 99999 /* size of audio input buffer in JACK FRAMES,
			   PER CHANNEL. change this size based 
			   on available system memory and intended
			   application. under-allocating frames 
			   (memory) will seg-fault your JACK client
			   when attempting to capture audio for
			   too long. Overallication will zombify your
			   JACK client if you queue too much data. */

#define OUT_FRAMES 99999 /* same as above EXCEPT for output and
			    this output buffer size is set
			    PER SOUND FILES ((NUM_SAMPLES). Each 
			    soundfile in this system possesses its own
			    buffer.  Under-allocating frames
			    (memory) will seg-fault your JACK
			    client.  Overallication will zombify your
			   JACK client if you queue too much data. */
#endif
