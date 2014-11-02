/* candor.c
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

'candor' is JACK-enabled realtime performance software for use
with a monome (http://www.monome.org/).
Copyright 2012 murray foster */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <monome.h>
#include <jack/jack.h>
#include <alsa/asoundlib.h>
#include <lo/lo.h>

#include "libficus.h"

unsigned int grid[16][16] = { [0 ... 15][0 ... 15] = 0 };
unsigned int mixer_row_grid[16][16] = { [0 ... 15][0 ... 15] = 0 };

/* following arrays keep track of Leds for each 'page'/mode for each device 
 sampler - sampler_page_pos
    [0] - playback
    [1] - loop
    [2] - kill playback per sample
    [3] - capture
    [4] - set capture time limit
    [5] - input mixer
    [6] - output mixer 
    [7] - kill all playback

 sequencer - sequencer_page_pos
    [0] - playhead view
    [1] - voices per 1st 8 steps
    [2] - voices per 2nd 8 steps
    [3] - voices per 3rd 8 steps
    [4] - voices per 4th 8 steps
    [5] - voices per 5th 8 steps
    [6] - voices per 6th 8 steps
    [7] - start/stop sequencer */

int sampler_page_pos[8] = {0};
int sampler_page_leds[48] = {0};
int sampler_loop_leds[48] = {0};

/* sampler_capture_leds[0] == 1 : armed
   sampler_capture_leds[1] == 1 : capturing */
int sampler_capture_leds[2][48] = {{0}};
/* used to load the sample after we're
   finished capturing it */
int sampler_capture_loadcheck[48] = {0};
int sampler_capture_armed_order[48] = {-1};
int sampler_capture_armed_pos = 0;
int sampler_capture_armed_pos_read = 0;
int sampler_capture_armed_count = 0;

/* sampler_capture_limit_leds[0] : limit multiple
   sampler_capture_limit_leds[1] : refresh storage   
   sampler_capture_limit_leds[2] : limit */
int sampler_capture_limit_leds[2] = {0};
int sampler_capture_limit=0;
int sampler_capture_limit_set=0;


int sampler_inmix_leds[48] = {0};
int sampler_outmix_leds[48] = {0};

int sampler_monitors[8] = {0};

int sequencer_page_pos[7] = {0};

/* these integers hold libficus' state */
int *playback_state;
int *loop_state;
int *capture_state;
int *limitcapture_state;
int *inputmix_state;
int *outputmix_state;

/* global path/prefix for sample storage on disk */
char *sampler_path=NULL;
char *sampler_prefix=NULL;

/* tap_recorder_leds[0]==0 off
   tap_recorder_leds[0]==1 armed
   tap_recorder_leds[1]==1 recording */
int tap_recorder_leds[2]={0};
/* tap recorder step storage */
int recorded_steps[15000000]={0};

/* signals sequencer to play next step */
int sequencer_nextstep_pretap=0;

/* sequencer playhead/position/tempo */
int seq_playhead=0;
int seq_stepcount=0;
float seq_bpm=0.0;
int seq_bpm_led=0;
int seq_bpm_dec_led=0;
int seq_bpm_inc_led=0;

int sequencer_voice_leds[6][48]={{0}};
int sequencer_bank_pos[7]={0};
int sequencer_bank_leds[6][48]={{0}};
int sequencer_bank_select=0;
int sequencer_transport_led = 0;

/* for samples assigned to this map,
   index starts at '1'.  '0' is 
   unassigned */
int sequencer_voice_map[6][48]={{0}};

float sampler_playback_speed=0;
int playback_speed_led=0;
int playback_reverse=0;

float sampler_playback_rampup=0;
float sampler_playback_rampdown=0;

int playback_rampup_led=0;
int playback_rampdown_led=0;
int playback_upnoclip=0;
int playback_downnoclip=0;

int playback_modifiers_enable[48]={0};

/* these are the speed and attack/decay ramps for
   each sample. given x is any sample;
   playback_modifiers[x][0] - speed
   ..[x][1] - attack
   ..[x][2] - decay 
*/
float playback_modifiers[48][3]={{0}};

int signal_playback_trigger[48] = {0};

/* these variables are for automagically hooking up monome */
char *monome_name;
char *monome_name_user_defined="init";
char *monome_device_type;
int monome_serialosc_port = 0;

/* serialosc host, default =  127.0.0.1, port: 12002 */
int osc_interface_disable = 0;

/* osc_port_out to be used by all outgoing osc messages */
char *osc_port_out = NULL;

/* button state used to indicate if we are accepting internal clock signal */
int external_clock_enable = 0;

void
init_default_state(monome_t *monome)
{
  /* on sampler, we start out on the 'playback' page.
     on sequencer, we start out on the first page */
  sequencer_page_pos[0]=1;
  sampler_page_pos[0]=1;

  sampler_capture_limit_leds[0]=0;
  /* 48 is OFF for the following two LED states */
  sampler_capture_limit_leds[1]=48;
  sampler_capture_limit_leds[2]=48;

  seq_bpm=7.5;
  monome_led_on(monome,0,6);

  sequencer_bank_pos[1]=1;

  sampler_playback_speed=1.0;
  playback_speed_led=4;

} /* init_default_state */

void
clear_frame(const monome_event_t *e, int xmod, int ymod)
{
  /* black out top 6 rows of interactive LEDs */
  int x, y;
  for( x=0; x<8; x++)
    for( y=0; y<6; y++)
      monome_led_off(e->monome, x+xmod, y+ymod);

} /* clear_frame */
 
void
clear_frame_monome(monome_t *monome, int xmod, int ymod)
{
  /* black out top 6 rows of interactive LEDs */
  int x, y;
  for( x=0; x<8; x++)
    for( y=0; y<6; y++)
      monome_led_off(monome, x+xmod, y+ymod);

} /* clear_frame_monome */

int
coordinate_to_led(int x, int y)
{
  return (x + 1) * (y + 1) + (7 * y) - (x * y) - 1;
} /* coordinate_to_led */

void
clear_armed()
{
  int c=0;

  /* clear all samples 'armed' for capturing */
  for(c=0; c<48; c++)
    sampler_capture_leds[0][c] = 0;
  memset(sampler_capture_armed_order,-1,48);
  sampler_capture_armed_count = 0;
  sampler_capture_armed_pos  = 0;
  sampler_capture_armed_pos_read = 0;
} /* clear_armed */

void
fill_sampler_row(monome_t *monome, int y, int x, int xmod)
{
  int i;
  for(i=0; i<x; i++)
    monome_led_on(monome, i+xmod, y);
} /* fill_sampler_row */

void
fill_to_button(monome_t *monome, int button)
{
  /* fill led rows of this quadrant to the given button */
  int x, y;
  
  /* first, clear all LEDs */
  clear_frame_monome(monome, 8, 0);

  /* figure out how many LEDs to light up and do it */
  if( button < 8 )
    fill_sampler_row(monome, 0, button+1, 8);
  else
    if( (button < 16) && (button > 7) )
      {
	fill_sampler_row(monome, 0, 8, 8);
	fill_sampler_row(monome, 1, button-7, 8);
      }
    else
      if( (button < 24) && (button > 15))
	{
	  fill_sampler_row(monome, 0, 8, 8);
	  fill_sampler_row(monome, 1, 8, 8);
	  fill_sampler_row(monome, 2, button-15, 8);
	}
      else
	if( (button < 32) && (button > 23) )
	  {
	    fill_sampler_row(monome, 0, 8, 8);
	    fill_sampler_row(monome, 1, 8, 8);
	    fill_sampler_row(monome, 2, 8, 8);
	    fill_sampler_row(monome, 3, button-23, 8);
	  }
	else
	  if( (button < 40) && (button > 31) )
	    {
	      fill_sampler_row(monome, 0, 8, 8);
	      fill_sampler_row(monome, 1, 8, 8);
	      fill_sampler_row(monome, 2, 8, 8);
	      fill_sampler_row(monome, 3, 8, 8);
	      fill_sampler_row(monome, 4, button-31, 8);
	    }
	  else
	      if( (button < 48) && (button > 39) )
		{
		  fill_sampler_row(monome, 0, 8, 8);
		  fill_sampler_row(monome, 1, 8, 8);
		  fill_sampler_row(monome, 2, 8, 8);
		  fill_sampler_row(monome, 3, 8, 8);
		  fill_sampler_row(monome, 4, 8, 8);
		  fill_sampler_row(monome, 5, button-39, 8);
		}
	      else
		fprintf(stderr, "candor: fill_to_button() is FUBARD\n");
  
} /* fill_to_button */

void
set_input_group(int bank, int state)
{
  int c=0;

  if( bank<8 )
    for(c=0; c<8; c++)
      ficus_setmixin(c, bank, state);
  else
    if( bank<16 )
      for(c=8; c<16; c++)
	ficus_setmixin(c, bank-8, state);
    else
      if( bank<24 )
	for(c=16; c<24; c++)
	  ficus_setmixin(c, bank-16, state);
      else
	if( bank<32 )
	  for(c=24; c<32; c++)
	    ficus_setmixin(c, bank-24, state);
	else
	  if( bank<40 )
	    for(c=32; c<40; c++)
	      ficus_setmixin(c, bank-32, state);
	  else
	    if( bank<48 )
	      for(c=40; c<48; c++)
		ficus_setmixin(c, bank-40, state);
} /* set_input_group */

void
set_output_group(int bank, int state)
{
  int c=0;

  if( bank<8 )
    for(c=0; c<8; c++)
      ficus_setmixout(c, bank, state);
  else
    if( bank<16 )
      for(c=8; c<16; c++)
	ficus_setmixout(c, bank-8, state);
    else
      if( bank<24 )
	for(c=16; c<24; c++)
	  ficus_setmixout(c, bank-16, state);
      else
	if( bank<32 )
	  for(c=24; c<32; c++)
	    ficus_setmixout(c, bank-24, state);
	else
	  if( bank<40 )
	    for(c=32; c<40; c++)
	      ficus_setmixout(c, bank-32, state);
	  else
	    if( bank<48 )
	      for(c=40; c<48; c++)
		ficus_setmixout(c, bank-40, state);
} /* set_output_group */

int candor_playback(int samplenum)
{
  int button=0;
  
  button=samplenum;

  /* we need to put the ficus modifier calls here */
  if(playback_modifiers_enable[button])
    {
      ficus_playback_speed(button,playback_modifiers[button][0]);
      ficus_playback_rampup(button,playback_modifiers[button][1]);
      ficus_playback_rampdown(button,playback_modifiers[button][2]);
    }
  else
    {
      ficus_playback_speed(button,1);
      ficus_playback_rampup(button,0.0);
      ficus_playback_rampdown(button,0.0);
    }
  ficus_playback(button);

  return 0;

}/* candor_playback */

int
sampler_page_chooser(const monome_event_t *e, int button)
{
  unsigned int c, page=0;
  int finallimit=0;
  char *tempstring;
  /* store our bank number */
  for(c=0; c<7; c++)
    if( sampler_page_pos[c] )
      page = c;

  /* triggers appropriate actions based on given
     linear button number and selected mode */

  /* sampler */
  switch( page )
    {
    /* playback page */
    case 0:
      /* if you hold down the 'playback' menu button, and 
	 press one of the sample pads, if that sample is currently
	 playing it will kill the playback for that sample. */
      if(grid[8][7]==1)
	ficus_killplayback(button);
      else
	{
	  playback_modifiers[button][1]=sampler_playback_rampup;
	  playback_modifiers[button][2]=sampler_playback_rampdown;
	  /* set speed/ramp parameters */
	  playback_modifiers[button][0]=sampler_playback_speed;
	  if( playback_reverse )
	    playback_modifiers[button][0]*=-1;
	  candor_playback(button);
	}
      break;
    case 1:
      /* looping page */
      if( ficus_islooping(button) )
	ficus_loop(button, 0);
      else
	ficus_loop(button, 1);
      break;
    case 2:
      /* toggle modifiers page */
      playback_modifiers_enable[button]=!playback_modifiers_enable[button];
      /* set speed/ramp parameters */
      playback_modifiers[button][0]=sampler_playback_speed;
      if( playback_reverse )
	playback_modifiers[button][0]*=-1;
      playback_modifiers[button][1]=sampler_playback_rampup;
      playback_modifiers[button][2]=sampler_playback_rampdown;
      break;
    case 3:
      /* capture page */
      if( !sampler_capture_leds[0][button] && 
	  sampler_capture_leds[1][button] )
	ficus_killcapture(button);
      else
	if( sampler_capture_leds[0][button] &&
	    !sampler_capture_leds[1][button] )
	  {
	    /* ficus_capturef_in captures an audio file to a bank for n 
	       number of frames.  we calculate the recording time IF there
	       is a set limit. */

	    /* impose a capture limit if one is set */
	    if (!sampler_capture_limit)
	      finallimit=0;
	    else
	      finallimit=ficus_durationf_out(sampler_capture_limit) * 
		(sampler_capture_limit_leds[0] + 1);
	    ficus_capturef(button, finallimit);
	    sampler_capture_leds[0][button]=0;
	    /* we have to wait to load this sample until we're
	       done recording so we add a check for it in our 
	       state_manager() and wait on it */
	    
	    /* WE NEED TO WRITE THE NEW SOUNDFILE TO A TEMPORARY
	       FILE AND THEN REPLACE IT WHEN DONE RECORDING USING
	       A FILESYSTEM OPERATION SO WE AVOID PLAYBACK OF 
	       AUDIO THAT'S NOT FINISHED CAPTURING.  THIS IS TO
	       BE DONE (PROBABLY) IN libficus.c 
	        
               ^^badly worded, but you'll remember (looping) */

	    sampler_capture_loadcheck[button]=1;
	    sampler_capture_armed_count = sampler_capture_armed_count - 1;
	    for( c=0; c<48; c++)
	    if( sampler_capture_armed_order[c] == button )
	      sampler_capture_armed_order[c] = -1;
	  }
	else
	  if( !sampler_capture_leds[0][button] &&
	      !sampler_capture_leds[1][button] )
	    {
	      sampler_capture_leds[0][button] = 1;
	      sampler_capture_armed_order[sampler_capture_armed_pos] = button;
	      sampler_capture_armed_count = sampler_capture_armed_count++;
	      sampler_capture_armed_pos = sampler_capture_armed_pos++;
	      if( sampler_capture_armed_pos == 48 )
		sampler_capture_armed_pos = 0;
	    }
	  else
	   fprintf(stderr, "candor: capture page is FUBARD\n");                                 
      break;
    case 4:
      if (grid[12][7]==1)
	{
	  if( sampler_capture_limit_leds[2]==button )
	    {
	      sampler_capture_limit_leds[2]=48;
	      /* we set the capture_limits_led[1] and [2] to 48
		 to trigger a refresh on the page so we
		 don't have LEDs out that we need on. */
	      sampler_capture_limit_leds[1]=48;
	      sampler_capture_limit=48;
	      sampler_capture_limit_set=0;
	    }
	  else
	    {
	      sampler_capture_limit_leds[2]=button;
	      sampler_capture_limit=button;
	      sampler_capture_limit_set=1;
	      /* see explanation in the corresponding
		 'if' to this 'else' */
	      sampler_capture_limit_leds[1]=48;
	    }
	}
      else
	{
	  sampler_capture_limit_leds[0]=button;
	  sampler_capture_limit_set=0;
	  break;
	}
    case 5:
      /* toggle mix in for samples */
      if( sampler_inmix_leds[button] )
	{
	  sampler_inmix_leds[button]=0;
	  set_input_group(button, 0);
	}
      else
	{
	  sampler_inmix_leds[button]=1;
	  set_input_group(button, 1);
	}
      break;
    case 6:
      /* toggle mix out for samples */
      if( sampler_outmix_leds[button] )
	{
	  sampler_outmix_leds[button]=0;
	  set_output_group(button, 0);
	}
      else
	{
	  sampler_outmix_leds[button]=1;
	  set_output_group(button, 1);
	}
      break;
    }
  
  return 0;
} /* sampler_page_chooser */

int
sequencer_page_chooser(const monome_event_t *e, int button)
{
  unsigned int c, page=0;
  
  /* store our bank number */
  for(c=0; c<7; c++)
    if( sequencer_page_pos[c] )
      page = c;
 
  if( sequencer_voice_leds[page-1][button] )
    sequencer_voice_leds[page-1][button]=0;
  else
    sequencer_voice_leds[page-1][button]=1;
  
} /* sequencer_page_chooser */

int
togglevoice_page_chooser(const monome_event_t *e, int button)
{
  unsigned int c, page=0;
  int x=0;
  int tempint=0;
  char *tempstring;
  
  /* store our bank number */
  for(c=0; c<7; c++)
    if( sequencer_bank_pos[c] )
      page = c;
 
  if(sequencer_voice_map[page-1][button]==sequencer_bank_select+1)
    sequencer_voice_map[page-1][button]=0;
  else
    {
      /*this bit makes sure we only assign once sample per step*/
      /* i'm looking at it now and slightly inebriated so i'm short
	 on patience but this converts a [48][6]-type array to [6][48]
	 which allows us to toggle-off/forget the previously set 
	 sequencer_bank_select and set the new one. sorry. */
      x=button%8;
      for(c=0;c<7;c++)
	if( sequencer_voice_map[page-1][x+(c*8)]==sequencer_bank_select+1)
	  sequencer_voice_map[page-1][x+(c*8)]=0;
      sequencer_voice_map[page-1][button]=sequencer_bank_select+1;
    }
} /* togglevoice_page_chooser */

int
togglebank_page_chooser(const monome_event_t *e, int button)
{
  sequencer_bank_select=button;
} /* togglebank_voice_chooser */

void
handle_press(const monome_event_t *e, void *data) 
{
  unsigned int x, y, x2, y2, button, c;

  x = e->grid.x;
  y = e->grid.y;

  /* store monome state change */
  grid[x][y]=1;

  /* SEQUENCER, top-left quadrant filtering */
  if( (x < 8) && (y < 8) )
    {
      /* convert matrix indeces (ex. (2, 5), (0, 0)) to linear
	 sample number (ex. 42, 0) */
      button = coordinate_to_led(x, y);

      /* SEQUENCER, top 48 pads */
      if( button < 48 && sequencer_page_pos[0] == 0)
	{
	  /* only do this if we're NOT on the first sequencer page */
	  sequencer_page_chooser(e, button);
	}

      if( grid[0][7] && button==55)
	{
	  if (external_clock_enable == 0)
	    external_clock_enable = 1;
	  else
	    external_clock_enable = 0;
	  return;
	}

      if ( (button>47) && (button<56) && (external_clock_enable==0))
	{
	  /* SEQUENCER, bpm row */
	  seq_bpm_led=button-47;
	  switch(button-48)
	    {
	    case 0:
	      seq_bpm=15;
	      break;
	    case 1:
	      seq_bpm=30;
	      break;
	    case 2:
	      seq_bpm=60;
	      break;
	    case 3:
	      seq_bpm=120;
	      break;
	    case 4:
	      seq_bpm=240;
	      break;
	    case 5:
	      seq_bpm=480;
	      break;
	    case 6:
	      seq_bpm=600;
	      break;
	    case 7:
	      seq_bpm=700;
	      break;
	    }
	}
   
      if( (button >= 56) && (button < 63) )
	{
	  /* clear and set new current sequencer function  */
	  if( !sequencer_page_pos[button-56] )
	    {
	      for( c = 0; c < 7; c++)
		sequencer_page_pos[c] = 0;
	      sequencer_page_pos[button - 56] = 1;
	      clear_frame(e, 0, 0);
	    }
	}

      /* clear tap recorder and arm for recording */
      if( grid[0][7] && button==63 && (external_clock_enable==0))
	if( !tap_recorder_leds[1] && !tap_recorder_leds[0] ) 
	  tap_recorder_leds[0]=1;	
	else
	  if( !tap_recorder_leds[1] && tap_recorder_leds[0] )
	    {
	      tap_recorder_leds[1]=1;
	      tap_recorder_leds[2]=0;
	    }
          else
	    if( tap_recorder_leds[1] && tap_recorder_leds[0] )
	      {
		tap_recorder_leds[0]=0;
		tap_recorder_leds[1]=0;
		tap_recorder_leds[2]=0;
	      }
      /* record if we enter 'armed' mode and we start tapping */
      if( button==63 && (external_clock_enable==0))
	{
	  if( (!tap_recorder_leds[1] && tap_recorder_leds[0]) 
	      && !tap_recorder_leds[2])
	    {
	      tap_recorder_leds[2]=1;
	    }
	  else
	    if( (tap_recorder_leds[0] && !tap_recorder_leds[1])
		&& tap_recorder_leds[2] )
	      {
		sequencer_nextstep_pretap=1;
	      }
	}
    }
  
  /* SAMPLER, top-right quadrant filtering */
  if( (x > 7) && (y < 8) )
    {
      /* adjust equation inputs for offset of in-use monome quadrant 
	 (read: i'm too lazy to adjust the equation) */
      x2 = x - 8;
      /* convert matrix indeces (ex. (10, 5), (8, 0)) to linear
	 sample number (ex. 42, 0) */
      button = coordinate_to_led(x2, y);

      /* SAMPLER, top 48 pads */
      if( button < 48 )
	  sampler_page_chooser(e, button);

      /* SAMPLER, turn jack monitors on or off (8,  6) .. (15, 6) */
      if( (button >= 48) && (button < 56) )
	{
	  mixer_row_grid[x][y] = !mixer_row_grid[x][y];
	  monome_led_set(e->monome, x, y, mixer_row_grid[x][y]);
	  if(sampler_monitors[button-48])
	    {
	      ficus_jackmonitor(button-48, 0, 0);
	      ficus_jackmonitor(button-48, 1, 0);
	      sampler_monitors[button-48]=0;
	    }
	  else
	    {
	      ficus_jackmonitor(button-48, 0, 1);
	      ficus_jackmonitor(button-48, 1, 1);
	      sampler_monitors[button-48]=1;
	    }
	  return;
	}  

      /* SAMPLER control row (8, 7) .. (15, 7) */
      if( (button >= 56) && (button < 63) )
	{
	  /* clear and set new current sampler function  */

	  /* pressing the sampler's capture mode control button 
	     (11, 7) after already entering the capture mode
	     will set 'armed' samples set for capturing to an 
	     'unarmed' state. */
	  if( ((button-56)==3) && sampler_page_pos[3])
	    clear_armed();

	  if( (button-56) == 4 )
	    sampler_capture_limit_leds[1]=48;

	  for( c = 0; c < 8; c++)
	    sampler_page_pos[c] = 0;
	  sampler_page_pos[button - 56] = 1;
	  
	  clear_frame(e, 8, 0);
	  
	  return;
	}
      
      /* SAMPLER global mute button */
      if( button == 63 )
	{
	  monome_led_set(e->monome, x, y, 1);
	  for( c=0; c<48; c++)
	    ficus_killplayback(c);
	  return;
	}
    }

  /* SEQUENCER-BANKS, bottom-left quadrant filtering */
  if( (x < 8) && (y > 7) )
    {
      y2=y-8;
      button=coordinate_to_led(x,y2);

      /* SEQUENCER-BANKS, top 48 pads */
      if( button < 48 )
	togglevoice_page_chooser(e, button);

      /* SAMPLER-SEQUENCER, playback row */
      if( (button>47) && (button<56) )
	{
	  playback_speed_led=button-47;
	  switch(button-48)
	    {
	    case 0:;
	      if((sampler_playback_speed==(float)1)&&!playback_reverse)
		playback_reverse=1;
	      else
		if(sampler_playback_speed==(float)1)
		  playback_reverse=0;
	      sampler_playback_speed=.25;
	      break;
	    case 1:
	      sampler_playback_speed=.50;
	      break;
	    case 2:
	      sampler_playback_speed=.74;
	      break;
	    case 3:
	      sampler_playback_speed=1;
	      break;
	    case 4:
	      sampler_playback_speed=1.25;
	      break;
	    case 5:
	      sampler_playback_speed=1.50;
	      break;
	    case 6:
	      sampler_playback_speed=1.75;
	      break;
	    case 7:
	      sampler_playback_speed=2.0;
	      break;
	    }
	}

      if(button==56)
	{
	  seq_bpm-=1;
	  seq_bpm_dec_led=1;
	}

      if(button==63)
	{
	  seq_bpm+=1;
	  seq_bpm_inc_led=1;
	}

      /* SEQUENCER-BANKS,  control row (0, 15) .. (7, 15) */
      if( (button > 56) && (button < 63) )
	{
	  /* clear and set new current sequencer function  */
	  for( c = 1; c < 7; c++)
	    sequencer_bank_pos[c] = 0;
	  sequencer_bank_pos[button - 56] = 1;
	  clear_frame(e, 0, 8);
	}
    }  

  /* SEQUENCER-VOICE-SELECT, bottom-right quadrant filtering */
  if( (x>7) && (y>7) )
    {
      x2=x-8;
      y2=y-8;
      button=coordinate_to_led(x2,y2);

      /* SEQUENCER-VOICE-SELECT, top 48 pads */
      if( button < 48 )
	{
	  togglebank_page_chooser(e, button);
	  clear_frame(e,8,8);
	  clear_frame(e,0,8);
	}
      /* SAMPLER-SEQUENCER, playback row */
      if( (button>47) && (button<56) )
	{
	  if(!playback_upnoclip)
	     playback_rampup_led=button-47;
	  switch(button-48)
	    {
	    case 0:
	      if((playback_upnoclip==0)&&(sampler_playback_rampup==0))
		{
		  sampler_playback_rampup=.001;
		  playback_upnoclip=1;
		}
	      else
		{
		  playback_upnoclip=0;
		  sampler_playback_rampup=0;
		}
	      break;
	    case 1:
	      if(!playback_upnoclip)
		sampler_playback_rampup=.01;
	      break;
	    case 2:
	      if(!playback_upnoclip)
		sampler_playback_rampup=.05;
	      break;
	    case 3:
	      if(!playback_upnoclip)
		sampler_playback_rampup=.1;
	      break;
	    case 4:
	      if(!playback_upnoclip)
		sampler_playback_rampup=.25;
	      break;
	    case 5:
	      if(!playback_upnoclip)
		sampler_playback_rampup=.5;
	      break;
	    case 6:
	      if(!playback_upnoclip)
		sampler_playback_rampup=.75;
	      break;
	    case 7:
	      if(!playback_upnoclip)
		sampler_playback_rampup=.99;
	      break;
	    }
	}

      /* SAMPLER-SEQUENCER, playback row */
      if( (button>55) && (button<64) )
	{
	  if(!playback_downnoclip)
	     playback_rampdown_led=button-55;

	  switch(button-56)
	    {
	    case 0:
	      if((playback_downnoclip==0)&&(sampler_playback_rampdown==0))
		{
		  sampler_playback_rampdown=.001;
		  playback_downnoclip=1;
		}
	      else
		{
		  playback_downnoclip=0;
		  sampler_playback_rampdown=0;
		}
	      break;
	    case 1:
	      if(!playback_downnoclip)
		sampler_playback_rampdown=.01;
	      break;
	    case 2:
	      if(!playback_downnoclip)
		sampler_playback_rampdown=.05;
	      break;
	    case 3:
	      if(!playback_downnoclip)
		sampler_playback_rampdown=.1;
	      break;
	    case 4:
	      if(!playback_downnoclip)
		sampler_playback_rampdown=.25;
	      break;
	    case 5:
	      if(!playback_downnoclip)
		sampler_playback_rampdown=.5;
	      break;
	    case 6:
	      if(!playback_downnoclip)
		sampler_playback_rampdown=.75;
	      break;
	    case 7:
	      if(!playback_downnoclip)
		sampler_playback_rampdown=.99;
	      break;
	    }
	}
    }

} /* handle_press */

void
handle_lift(const monome_event_t *e, void *data)
{

  unsigned int x, y, x2;

  x = e->grid.x;
  y = e->grid.y;

  x2 = x - 8;

  /* store monome state change */
  grid[x][y]=0;

} /* handle_lift */

void
playback_led_state(monome_t *monome, int x, int y)
{
  int bank = coordinate_to_led(x-8, y);

  if( sampler_page_leds[bank] )
    monome_led_on(monome, x, y);
  else
    monome_led_off(monome, x, y);
} /* playback_led_state */

void
loop_led_state(monome_t *monome, int x, int y)
{
  int bank = coordinate_to_led(x-8, y);

  if( sampler_loop_leds[bank] )
    monome_led_on(monome, x, y);
  else
    monome_led_off(monome, x, y);
} /* loop_led_state */

void
modifier_led_state(monome_t *monome, int x, int y)
{
  int bank = coordinate_to_led(x-8, y);
  
  if( playback_modifiers_enable[bank] )
    monome_led_on(monome, x, y);
  else
    monome_led_off(monome, x, y);
} /* modifier_led_state */

void
capture_led_state(monome_t *monome, int x, int y)
{
  int bank = coordinate_to_led(x-8, y);
  int state = 0;

  if( sampler_capture_leds[0][bank] )
    {
      /* random state between 0 and 1 == blinking lights!,
       meant to indicate this sample is 'armed for capture */
      state = (int)rand()/(int)(5) % 2;
      monome_led_set(monome, x, y, state);
    }
  else
    if( sampler_capture_leds[1][bank] )
	monome_led_on(monome, x, y);
    else
      if( !sampler_capture_leds[0][bank] &&
	  !sampler_capture_leds[1][bank] )
	monome_led_off(monome, x, y);
} /* capture_led_state */

void
capture_limit_led_state(monome_t *monome, int x, int y)
{
  int state;
  int bank = coordinate_to_led(x-8, y);
  
  if( sampler_capture_limit_leds[2] == bank)
    {
      state = (int)rand()/(int)(5) % 2;
      monome_led_set(monome, x, y, state);
    }  
} /* capture_limit_led_state */

void 
inmix_led_state(monome_t *monome, int x, int y)
{
  int state;
  int bank = coordinate_to_led(x-8, y);

  if( sampler_inmix_leds[bank] == 1 )
    monome_led_on(monome, x, y);
  else
    monome_led_off(monome, x, y);
} /* inmix_led_state */

void 
outmix_led_state(monome_t *monome, int x, int y)
{
  int state;
  int bank = coordinate_to_led(x-8, y);

  if( sampler_outmix_leds[bank] == 1 )
    monome_led_on(monome, x, y);
  else
    monome_led_off(monome, x, y);
} /* outmix_led_state */

void 
playhead_led_refresh(monome_t *monome, int x, int y)
{
  int state;
  int bank = coordinate_to_led(x, y);
  clear_frame_monome(monome, 0, 0);
  if( seq_playhead == bank )
    monome_led_on(monome, x, y);
} /* playhead_led_refresh */

void
tap_recorder_led_state(monome_t *monome, int x, int y)
{
  int state;
  int bank = coordinate_to_led(x, y);
  
  if( tap_recorder_leds[0] && !tap_recorder_leds[1])
    {
      state = (int)rand()/(int)10 % 2;
      monome_led_set(monome, x, y, state);
    }
  else
    if( tap_recorder_leds[1] )
      monome_led_set(monome, x, y, 1);
    else
      monome_led_set(monome, x, y, 0);
} /* tap_recorder_led_state */

void
seq_transport_led_state(monome_t *monome, int x, int y)
{
  int state;
  int bank = coordinate_to_led(x, y);
  
  if( tap_recorder_leds[0] && !tap_recorder_leds[1])
    {}
  else
    if( tap_recorder_leds[1] )
      {}
    else
      if( sequencer_transport_led )
	monome_led_set(monome, x, y, 1);
      else
	monome_led_set(monome, x, y, 0);

} /* tap_recorder_led_state */

void
voice_select_led_state(monome_t *monome, int x, int y)
{
  int state, c;
  int bank = coordinate_to_led(x, y);
  int pagemode=0;

  for(c=0; c<7; c++)
    if(sequencer_page_pos[c])
      pagemode=c;
  
  if( sequencer_voice_leds[pagemode-1][bank] )
    monome_led_on(monome, x, y);
  else
    monome_led_off(monome, x, y);
} /* voice_select_led_state */

void
voice_assignment_led_state(monome_t *monome, int x, int y)
{
  int state, c;
  int y2=y-8;
  int bank = coordinate_to_led(x, y2);
  int pagemode=0;

  for(c=0; c<7; c++)
    if(sequencer_bank_pos[c])
      pagemode=c;
   
  /* just leds */
  if( sequencer_voice_map[pagemode-1][bank]==0)
    monome_led_off(monome, x, y);
  else
    if( sequencer_voice_map[pagemode-1][bank]==sequencer_bank_select+1 )
      monome_led_on(monome, x, y);
} /* voice_assignment_led_state */

void
selected_assignment_led_state(monome_t *monome, int x, int y)
{
  int c;
  int x2=x-8;
  int y2=y-8;
  int bank = coordinate_to_led(x2, y2);
  int pagemode=0;

  if(bank==sequencer_bank_select)
    monome_led_on(monome, x, y);

} /* selected_assignment_led_state */

void
button_to_coordinate(monome_t *monome, int button, int xmod, int ymod, void (*funcptr_led)(monome_t *monome, int, int))
{
  /* generic function to handle calling functions that
     require a button number converted to coordinates.
     accepts a function pointer as a parameter so we can 
     reuse this. */

  /* filter buttons down */
  if( button < 8 )
    (*funcptr_led)(monome, button+xmod, 0+ymod);
    else
      if( (button < 16) && (button > 7) )
	(*funcptr_led)(monome, button-8+xmod, 1+ymod);
      else
	if( (button < 24) && (button > 15))
	  (*funcptr_led)(monome, button-16+xmod, 2+ymod);
	else
	  if( (button < 32) && (button > 23) )
	    (*funcptr_led)(monome, button-24+xmod, 3+ymod);
	  else
	    if( (button < 40) && (button > 31) )
	      (*funcptr_led)(monome, button-32+xmod, 4+ymod);
	    else
	      if( (button < 48) && (button > 39) )
		(*funcptr_led)(monome, button-40+xmod, 5+ymod);
	      else
		if( (button < 56) && (button > 47) )
		  (*funcptr_led)(monome, button-48+xmod, 6+ymod);
		else
		  if( (button < 64) && (button > 55) )
		    (*funcptr_led)(monome, button-56+xmod, 7+ymod);
} /* button_to_coordinate */

void
sample_playback_trigger (int sample_num)
{
  candor_playback(sample_num);
  return;
} /* sample_playback_trigger */

void
state_change(monome_t *monome, int x, int y)
{
  char tempstring[100] = {0};

  /* monitors state changes of our audio engine
   and takes action when it needs to */

  /* derive sample bank from given coordinates */
  int bank = coordinate_to_led(x-8, y);

  /* SAMPLER,
     playback */
  if( !ficus_isplaying(bank) )
    sampler_page_leds[bank] = 0;
  else
    sampler_page_leds[bank] = 1;

  if( signal_playback_trigger[bank] ) {
    sample_playback_trigger(bank);
    signal_playback_trigger[bank] = 0;
  }

  /* looping */
  if( !ficus_islooping(bank) )
    sampler_loop_leds[bank] = 0;
  else
    sampler_loop_leds[bank] = 1;

  /* capturing */
  if( ficus_iscapturing(bank) )
    {
      sampler_capture_leds[0][bank]=0;
      sampler_capture_leds[1][bank]=1;
    }
  else
    {
      /* if we aren't capturing to bank, load new soundfile
	 and turn off corresponding LED */
      if( sampler_capture_loadcheck[bank] )
	{
	  /* load the sample after capturing is finished */
	  snprintf(tempstring, 100, "%s/%s%d.wav", sampler_path, sampler_prefix, bank);
	  ficus_loadfile(tempstring, bank);
	  sampler_capture_loadcheck[bank]=0;
	  /* if the sample is set to loop, we immediately
	     begin playback */
	  if( ficus_islooping(bank) )
	    candor_playback(bank);
	  sampler_capture_loadcheck[bank]=0;
	}
      sampler_capture_leds[1][bank]=0;
    }
} /* state_change */

void
playhead_nextstep(monome_t *monome)
{
  /* ***this function really exists for the tap recorder*** */

  if(sequencer_page_pos[0])
    {
      void (*funcptr_led_change)(monome_t *monome, int, int);
      funcptr_led_change=&playhead_led_refresh;
      button_to_coordinate(monome, seq_playhead, 0, 0, funcptr_led_change);
    }
  seq_playhead+=1;
  if( seq_playhead==48 )
    seq_playhead=0;
} /* playhead_nextstep */

void trigger_step_playback(int step)
{
  int i,c,j;

  if( step<8 )
    i=0;
  else
    if( step<16 )
      i=1;
    else
      if( step<24 )
	i=2;
      else
	if( step<32 )
	  i=3;
	else
	  if( step<40 )
	    i=4;
	  else
	    i=5;

  for(c=0; c<6; c++)
    { 
      j=step+(c*8)-(i*8);
      if( (sequencer_voice_leds[i][j]) &&
	  (sequencer_voice_map[i][j]) )
	signal_playback_trigger[sequencer_voice_map[i][j]-1] = 1;
    }
}/* trigger_step_playback */

lo_address get_outgoing_osc_addr()
{
  lo_address lo_addr_send;
  return lo_address_new("127.0.0.1",osc_port_out);
} /* get_outgoing_osc_addr */

void trigger_step(int step)
{
  lo_address lo_addr_send = get_outgoing_osc_addr();
  trigger_step_playback(step);

  if( external_clock_enable == 0 )
    if (lo_send(lo_addr_send,"/serialosc/clock","i",step) == -1 )
      fprintf(stderr,"ERROR sending message /candor/clock %d\n", step);

} /* trigger_step */

void
tap_recorder(monome_t *monome)
{
  int msdelay=10000; /*10000microseconds or 10milliseconds */
  int count=0;
  int stepcount=0;
  void (*funcptr_led_change)(monome_t *monome, int, int);

  while(tap_recorder_leds[2])
    {
      seq_stepcount=0;
      while(count<sizeof(recorded_steps))
	{
	  /* assigns a tap to the next available
	     index in our 'recorded steps'-queue */
	  if(sequencer_nextstep_pretap)
	    {
	      trigger_step(seq_playhead);
	      playhead_nextstep(monome);
	      recorded_steps[count]=1;
	      sequencer_nextstep_pretap=0;
	      seq_stepcount++;
	    }
	  else
	    recorded_steps[count]=0;
	  count++;
	  usleep(msdelay);
	  if(!tap_recorder_leds[2])
	    break;
	}
      count=0;
    }
  /* if we're playing steps, trigger next step of sequencer */
  while(tap_recorder_leds[1] )
    {
      while(count<sizeof(recorded_steps))
	{
	  if(recorded_steps[count])
	    {
	      playhead_nextstep(monome);
	      stepcount++;
	      trigger_step(seq_playhead);
	    }	  
	  if(stepcount==seq_stepcount)
	    {
	      stepcount=0;
	      count=0;
	    }
	  count++;
	  usleep(msdelay);
	  if( tap_recorder_leds[2] || !tap_recorder_leds[1] )
	    break;
	}
      count=0;
      stepcount=0;
    }
} /* tap_recorder */

void
metronome(monome_t *monome)
{
  int ms = 0;
  void (*funcptr_led_change)(monome_t *monome, int, int);

  while( external_clock_enable==0 )
    {
      if( !sequencer_transport_led)
	{
	  /* bpm=some_stored_bpm; */
	  if(!tap_recorder_leds[0] || !tap_recorder_leds[1])
	    {
	      
	      if( sequencer_page_pos[0]==1 )
		{
		  funcptr_led_change=&playhead_led_refresh;
		  button_to_coordinate(monome, seq_playhead, 0, 0, funcptr_led_change);
		}
	      
	      trigger_step(seq_playhead);
	      
	      ms=(60.0f / (float)seq_bpm) * 1000 * 1000;
	      usleep(ms);
	      seq_playhead+=1;
	      if(seq_playhead==48)
		seq_playhead=0;
	    }
	  if(tap_recorder_leds[2])
	    return;
	}
      else
	{
	  funcptr_led_change=&playhead_led_refresh;
	  button_to_coordinate(monome, seq_playhead, 0, 0, funcptr_led_change);
	  ms=(60.0f / (float)seq_bpm) * 1000 * 1000;
	  usleep(ms);
	}
    }
} /* metronome */

void
seq_transport_thread(monome_t *monome)
{
  while(1)
    {
      if( tap_recorder_leds[0] )
	tap_recorder(monome);
  
      if( external_clock_enable == 0)
	metronome(monome);
      else
	usleep(10);
    }
} /* seq_transport_thread */


void
state_manager(monome_t *monome)
{
  /* declare and assign function pointers */
  void (*funcptr_sampler_state)(monome_t *monome, int, int);
  void (*funcptr_led_change)(monome_t *monome, int, int);
  int state_tempo_inc=0;
  int state_tempo_dec=0;
  int blink_state=0;
  
  funcptr_sampler_state=&state_change;

  unsigned int c, i, bank, seq_bank, seq_voice_bank, last_button;
  bank=0;
  seq_bank=0;
  seq_voice_bank=0;

  int keyboardpress;

  /* poll for state changes (mostly LEDs) */
  do
    {
      /* SAMPLER, figure out which 'page' we're on. */
      for( i=0; i<8; i++)
	if(sampler_page_pos[i])
	  bank=i;
      
      /* SEQUENCER, figure out which 'page' we're on. */
      for( i=0; i<7; i++)
	if(sequencer_page_pos[i])
	  seq_bank=i;

      /* VOICE-TO-ASSIGN, figure out which 'page' we're on. */
      for( i=0; i<7; i++)
	if(sequencer_bank_pos[i])
	    seq_voice_bank=i;

      for( c=0; c<48; c++)
	{
	  /* check audio backend status */ 
	  button_to_coordinate(monome, c, 8, 0, funcptr_sampler_state);
	  
	  /* only VISIBLY manipulate LED state for each
	     function when that PAGE/MODE is CURRENTLY
	     SELECTED: */
	  /*              */
	  /*SAMPLER (top-right quadrant) LED STATES*/
	  /*              */
	  switch(bank)
	    {
	    case 0:
	      funcptr_led_change=&playback_led_state;
	      button_to_coordinate(monome, c, 8, 0, funcptr_led_change);
	      break;
	    case 1:
	      funcptr_led_change=&loop_led_state;
	      button_to_coordinate(monome, c, 8, 0, funcptr_led_change);
	      break;
	    case 2:
	      funcptr_led_change=&modifier_led_state;
	      button_to_coordinate(monome, c, 8, 0, funcptr_led_change);
	      break;
	    case 3:
	      funcptr_led_change=&capture_led_state;
	      button_to_coordinate(monome, c, 8, 0, funcptr_led_change);
	      break;
	    case 4:
	      if( sampler_capture_limit_leds[0] != sampler_capture_limit_leds[1] )
		{
		  fill_to_button(monome, sampler_capture_limit_leds[0]);
	          sampler_capture_limit_leds[1]=sampler_capture_limit_leds[0];
		}
	      funcptr_led_change=&capture_limit_led_state;
	      button_to_coordinate(monome, c, 8, 0, funcptr_led_change);
	      break;
	    case 5:
	      funcptr_led_change=&inmix_led_state;
	      button_to_coordinate(monome, c, 8, 0, funcptr_led_change);
	      break;
	    case 6:
	      funcptr_led_change=&outmix_led_state;
	      button_to_coordinate(monome, c, 8, 0, funcptr_led_change);
	      break;
	    }

	  /*              */
	  /*SEQUENCER (top-left quadrant) LED STATES*/
	  /*              */
	  switch(seq_bank)
	    {
	    case 1:
	      funcptr_led_change=&voice_select_led_state;
	      button_to_coordinate(monome, c, 0, 0, funcptr_led_change);
	      break;
	    case 2:
	      funcptr_led_change=&voice_select_led_state;
	      button_to_coordinate(monome, c, 0, 0, funcptr_led_change);
	      break;
	    case 3:
	      funcptr_led_change=&voice_select_led_state;
	      button_to_coordinate(monome, c, 0, 0, funcptr_led_change);
	      break;
	    case 4:
	      funcptr_led_change=&voice_select_led_state;
	      button_to_coordinate(monome, c, 0, 0, funcptr_led_change);
	      break;
	    case 5:
	      funcptr_led_change=&voice_select_led_state;
	      button_to_coordinate(monome, c, 0, 0, funcptr_led_change);
	      break;
	    case 6:
	      funcptr_led_change=&voice_select_led_state;
	      button_to_coordinate(monome, c, 0, 0, funcptr_led_change);
	      break;
	    }
	  /*              */
	  /*SELECT VOICE-TO-ASSIGN (bottom-left quadrant) LED STATES*/
	  /*              */
	  switch(seq_voice_bank)
	    {
	    case 1:
	      funcptr_led_change=&voice_assignment_led_state;
	      button_to_coordinate(monome, c, 0, 8, funcptr_led_change);
	      break;
	    case 2:
	      funcptr_led_change=&voice_assignment_led_state;
	      button_to_coordinate(monome, c, 0, 8, funcptr_led_change);
	      break;
	    case 3:
	      funcptr_led_change=&voice_assignment_led_state;
	      button_to_coordinate(monome, c, 0, 8, funcptr_led_change);
	      break;
	    case 4:
	      funcptr_led_change=&voice_assignment_led_state;
	      button_to_coordinate(monome, c, 0, 8, funcptr_led_change);
	      break;
	    case 5:
	      funcptr_led_change=&voice_assignment_led_state;
	      button_to_coordinate(monome, c, 0, 8, funcptr_led_change);
	      break;
	    case 6:
	      funcptr_led_change=&voice_assignment_led_state;
	      button_to_coordinate(monome, c, 0, 8, funcptr_led_change);
	      break;
	    }

	  /*      */
	  /* SEQUENCER-SAMPLE-SELECT */
	  /*      */
	  funcptr_led_change=&selected_assignment_led_state;
	  button_to_coordinate(monome, c, 8, 8, funcptr_led_change);
	}

      /* check tap recorder status */
      funcptr_led_change=&tap_recorder_led_state;
      button_to_coordinate(monome, 63, 0, 0, funcptr_led_change);

      /* check main sequencer transport status */
      funcptr_led_change=&seq_transport_led_state;
      button_to_coordinate(monome, 63, 0, 0, funcptr_led_change);

      /* check sequencer voice-assignment control row LEDs */
      for( c = 1; c < 7; c++)
	{
	  if( sequencer_bank_pos[c] == 1)
	    monome_led_on(monome, c, 15);
	  else
	    monome_led_off(monome, c, 15);
	}

      /* check sequencer control row LEDs */
      for( c = 0; c < 7; c++)
	{
	  if( sequencer_page_pos[c] == 1)
	    monome_led_on(monome, c, 7);
	  else
	    monome_led_off(monome, c, 7);
	}

      /* handle blink for external osc clock */
      if (external_clock_enable == 1)
	{
	  blink_state = (int)rand()/(int)(5) % 2;
	  monome_led_set(monome, 7, 6, blink_state);
	  if( sequencer_page_pos[0]==1 )
	    {
	      funcptr_led_change=&playhead_led_refresh;
	      button_to_coordinate(monome, seq_playhead, 0, 0, funcptr_led_change);
	    }
	}
      else
	/* fill sequencer tempo row LEDs */      
	for( c=0; c<8; c++)
	  if(seq_bpm_led==0)
	    monome_led_on(monome,0,6);
	  else
	    if(c<seq_bpm_led)
	      monome_led_on(monome,c,6);
	    else
	      monome_led_off(monome,c,6);

      /* check sampler control row LEDs */
      for( c = 0; c < 8; c++)
	{
	  if( sampler_page_pos[c] == 1)
	    monome_led_on(monome, c+8, 7);
	  else
	    monome_led_off(monome, c+8, 7);
	}

      /* fill playback-speed control row LEDs */
      for( c=0; c<8; c++)
	if( (c<=playback_speed_led-1) && (c>=3) )
	  monome_led_on(monome,c,14);
	else
	  if( (c>=playback_speed_led-1) && (c<=3) )
	    monome_led_on(monome,c,14);
	  else
	    if(c==playback_speed_led-1)
	      monome_led_on(monome,c,14);
	    else
	      monome_led_off(monome,c,14);

      /* fill sampler attack ramp LEDs */      
      for( c=0; c<8; c++)
	if(playback_rampup_led==0)
	  monome_led_on(monome,8,14);
	else
	  if(c<playback_rampup_led)
	    monome_led_on(monome,c+8,14);
	  else
	    monome_led_off(monome,c+8,14);

      /* fill sampler decay ramp LEDs */
      for( c=0; c<8; c++)
	if(playback_rampdown_led==0)
	  monome_led_on(monome,8,15);
	else
	  if( c<playback_rampdown_led )
	    monome_led_on(monome,c+8,15);
	  else
	    monome_led_off(monome,c+8,15);

      /* handle blink for tap tempo decrease */
      if(seq_bpm_dec_led)
	{
	  state_tempo_dec++;
	  monome_led_on(monome,0,15);
	  if(state_tempo_dec==2)
	    {
	      monome_led_off(monome,0,15);
	      state_tempo_dec=0;
	      seq_bpm_dec_led=0;
	    }
	}

      /* handle blink for tap tempo increase */
      if(seq_bpm_inc_led)
	{
	  state_tempo_inc++;
	  monome_led_on(monome,7,15);
	  if(state_tempo_inc==2)
	    {
	      monome_led_off(monome,7,15);
	      state_tempo_inc=0;
	      seq_bpm_inc_led=0;
	    }
	}

      /* handle blink for attack ramp's no-clip function */
      if(playback_upnoclip)
	{
	  blink_state = (int)rand()/(int)(5) % 2;
	  monome_led_set(monome, 8, 14, blink_state);
	}
      else
	monome_led_on(monome,8,14);

      /* handle blink for decay ramp's no-clip function */
      if(playback_downnoclip)
	{
	  blink_state = (int)rand()/(int)(5) % 2;
	  monome_led_set(monome, 8, 15, blink_state);
	}
      else
	monome_led_on(monome,8,15);
      
      /* handle blink for reverse playback speed function */
      for(c=1;c<=8;c++)
	if(playback_reverse&&(c==playback_speed_led))
	  {
	    blink_state = (int)rand()/(int)(5) % 2;
	    monome_led_set(monome, c-1, 14, blink_state);
	  }

      /* handle blink for reverse playback speed function */
      for(c=1;c<=8;c++)
	if(playback_reverse&&(c==playback_speed_led))
	  {
	    blink_state = (int)rand()/(int)(5) % 2;
	    monome_led_set(monome, c-1, 14, blink_state);
	  }

      /* sending too many messages to serialoscd
	 will blow it up, so we need to rest a
	 few thousand nanoseconds */
      usleep(99999);

    }
  while(1);  
} /* state_manager */

void
trigger_active_capture_samples()
{
  int c = 0;
  int armed_count = 0;
  int bank = 0;
  int finallimit=0;
  int passthrough = 0;
  
  bank = sampler_capture_armed_order[sampler_capture_armed_pos_read];

  if( bank != -1 ) {
    if( !sampler_capture_leds[0][bank] && 
	sampler_capture_leds[1][bank] )
      {
	ficus_killcapture(bank);
	sampler_capture_armed_order[bank] = -1;
	sampler_capture_armed_count = sampler_capture_armed_count - 1;
	sampler_capture_armed_pos_read = sampler_capture_armed_pos_read++;
	if( sampler_capture_armed_pos_read == 48 )
	  sampler_capture_armed_pos_read = 0;
	if(sampler_capture_armed_count > 0) {
	  bank = sampler_capture_armed_order[sampler_capture_armed_pos_read];
	  passthrough = 1;
	}
	else
	  return;
      }
    
    if( passthrough || (sampler_capture_leds[0][bank] &&
	 !sampler_capture_leds[1][bank]) )
      {
	/* ficus_capturef_in captures an audio file to a bank for n 
	   number of frames.  we calculate the recording time IF there
	   is a set limit. */
	
	/* impose a capture limit if one is set */
	if (!sampler_capture_limit_set)
	  finallimit=0;
	else
	  finallimit=ficus_durationf_out(sampler_capture_limit) * 
	    (sampler_capture_limit_leds[0] + 1);
	
	ficus_capturef(bank, finallimit);
	sampler_capture_leds[0][bank]=0;
	/* we have to wait to load this sample until we're
	   done recording so we add a check for it in our 
	   state_manager() and wait on it */
	
	/* WE NEED TO WRITE THE NEW SOUNDFILE TO A TEMPORARY
	   FILE AND THEN REPLACE IT WHEN DONE RECORDING USING
	   A FILESYSTEM OPERATION SO WE AVOID PLAYBACK OF 
	   AUDIO THAT'S NOT FINISHED CAPTURING.  THIS IS TO
	   BE DONE (PROBABLY) IN libficus.c 
	   
	   ^^badly worded, but you'll remember (looping) */
	
	sampler_capture_loadcheck[bank]=1;
      }
  } else {
    sampler_capture_armed_pos_read = sampler_capture_armed_pos_read++;
    if( sampler_capture_armed_pos_read == 0 )
      sampler_capture_armed_pos_read = 0;
  }
} /* trigger_active_capture_samples */

int
process_alsa_rawmidi(char *portname)
{
  int status;
  int mode = SND_RAWMIDI_SYNC;

  snd_rawmidi_t* midiin = NULL;
    
  if ((status = snd_rawmidi_open(&midiin, NULL, portname, 0)) < 0) {
    fprintf(stderr, "candor: problem opening raw midi input: %s", snd_strerror(status));
    exit(1);
  }

  char buffer[1];        // Storage for input buffer received
  char midimessage[3]={0};
  int msg_count;
  while (1) 
    {
    if ((status = snd_rawmidi_read(midiin, buffer, 1)) < 0) {
      fprintf(stderr, "candor: problem reading raw midi input: %s", snd_strerror(status));
    }
    if ((unsigned char)buffer[0] >= 0x80) { 
      /* command byte */
      midimessage[0]=(unsigned char)buffer[0];
      msg_count=0;
    } 
    else 
      {
	msg_count++;
	midimessage[msg_count]=(unsigned char)buffer[0];
      }
    if(msg_count==2)
      {
	if( (unsigned char)midimessage[0]==176 )
	  {

	    /* CANDOR RAWMIDI HANDLERS */
	    if( (unsigned char)midimessage[1]==0 ) 
	      /* TRIGGER ARMED SAMPLES (FOR RECORDING) VIA MIDI CTRLR */
	      trigger_active_capture_samples();
	    else
	      if( (unsigned char)midimessage[1]==1 )
		/* START/STOP SEQUENCER TRANSPORT VIA MIDI CTRLR */
		{ 
		  if( !tap_recorder_leds[1] && !tap_recorder_leds[0] ) 
		    {
		      tap_recorder_leds[0]=1;
		      tap_recorder_leds[1]=1;
		    }
		  else
		    if( !tap_recorder_leds[1] && tap_recorder_leds[0] )
		      {
			tap_recorder_leds[1]=1;
			tap_recorder_leds[2]=0;
		      }
		    else
		      if( tap_recorder_leds[1] && tap_recorder_leds[0] )
			{
			  tap_recorder_leds[0]=0;
			  tap_recorder_leds[1]=0;
			  tap_recorder_leds[2]=0;
			  
			}
		}
	  }   
	//fprintf(stdout, "candor: rawmidi recv '%s': cmd0x%x #%d vel%d\n", 
	//	portname,
	//        (unsigned char)midimessage[0],
	//	    (unsigned char)midimessage[1],
	//           (unsigned char)midimessage[2]);
	msg_count=0;
      }
    fflush(stdout);
    }
  
  snd_rawmidi_close(midiin);
  midiin  = NULL;    /* snd_rawmidi_close() does not clear invalid pointer, */
  return 0;          /*  so might be a good idea to erase it after closing. */
  
} /* process_alsa_rawmidi */

void
setup_candor(monome_t *monome, char *name, char *path, char *prefix,
	     int bitdepth, char *rawmidi_device)
{
  /* begin 'state manager' thread */
  pthread_t state_thread_id;
  pthread_create(&state_thread_id, NULL, state_manager, monome);
  pthread_detach(&state_thread_id);

  /* sets default state of candor */
  init_default_state(monome);

  /* libficus setup */
  if (ficus_setup(name, path, prefix, bitdepth) == 1)
   {
     fprintf(stderr, "candor: libficus setup failed.\n");
     return;
   }

  /* begin sequencer metronome thread */
  pthread_t transport_thread_id;
  pthread_create(&transport_thread_id, NULL, seq_transport_thread, monome);
  pthread_detach(&transport_thread_id);
  /* if we have a specified device to use with alsa raw midi, hook it up */
  if( rawmidi_device != NULL )
    {
      pthread_t rawmidi_thread_id;
      pthread_create(&rawmidi_thread_id, NULL, process_alsa_rawmidi, rawmidi_device);
      pthread_detach(&rawmidi_thread_id);
    }
} /* setup_candor */

void
show_usage()
{
} /* show_usage */

int
load_from_file(char *path)
{
  FILE *infile;
  char string_buffer[200+1];
  char *filepath;
  int bank;

  /* (try to) read file */
  infile=fopen(path, "r");
  if( infile!=NULL )
    while( fgets(string_buffer, 200, infile)!=NULL)
      {
	sscanf(string_buffer, "%s %d\n", filepath, &bank);
        ficus_loadfile(filepath, bank);
      }
  else
    /* return error */
    return 1;
  
  return 0;
} /* paths_from_file */

/* BEGIN OPEN SOUND CONTROL INTERFACE */
/* this will probably replace the midi and we'll either have a multi-interface-to-osc daemon or a set of pd patches or something */

void error(int num, const char *msg, const char *path)
{
  printf("liblo server error %d in path %s: %s\n", num, path, msg);
  fflush(stdout);
}

/* catch any incoming messages and display them. returning 1 means that the
 * message has not been fully handled and the server should try other methods */
int generic_handler(const char *path, const char *types, lo_arg ** argv,
                    int argc, void *data, void *user_data)
{
  int i;
  
  fprintf(stdout,"path: <%s>\n", path);
  for (i = 0; i < argc; i++) {
    fprintf(stderr,"arg %d '%c' ", i, types[i]);
    lo_arg_pp((lo_type)types[i], argv[i]);
    fprintf(stdout,"\n");
  }
  return 0;
}

int osc_external_clock_handler(const char *path, const char *types, lo_arg ** argv,
				 int argc, void *data, void *userdata)
{ 
  int ms = 0;

  if( external_clock_enable == 1 )
    {
      trigger_step(seq_playhead);
      seq_playhead+=1;
      if(seq_playhead==48)
	seq_playhead=0;
    }
      
  return 0;
} /* osc_external_clock_handler */


int osc_serialosc_device_handler(const char *path, const char *types, lo_arg ** argv,
				 int argc, void *data, void *user_data)
{ 
  monome_name=argv[0]; /* incoming monome name */

  /* if we have a user-defined monome name, check incoming device */
  if( monome_name_user_defined != "init" )
    {
      if( strcmp(monome_name, monome_name_user_defined) == 0 )
	{
	  monome_device_type=argv[1]->s;
	  monome_serialosc_port=argv[2]->i;
	}
    }
  else
    {
      monome_device_type=argv[1]->s;
      monome_serialosc_port=argv[2]->i;
    }
  
  return 0;
} /* osc_serialosc_device_handler */

int osc_load_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  char *filepath;
  int samplenum;
  fprintf(stdout, "path: <%s>\n", path);
  filepath=argv[0]->s;
  samplenum=argv[1]->i;
  ficus_loadfile(filepath,samplenum);
  return 0;
} /* osc_load_handler */

int osc_loop_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  int samplenum;
  int loopstate;
  fprintf(stdout, "path: <%s>\n", path);
  samplenum=argv[0]->i;
  loopstate=argv[1]->i;
  ficus_loop(samplenum,loopstate);
  return 0;
} /* osc_load_handler */

int osc_setmixout_handler(const char *path, const char *types, lo_arg ** argv,
			  int argc, void *data, void *user_data)
{
  int i;
  int sample,channel,to_set;
  fprintf(stdout,"path: <%s>\n", path);
  sample=argv[0]->i;
  channel=argv[1]->i;
  to_set=argv[2]->i;
  ficus_setmixout(sample,channel,to_set);
  return 0;
} /* osc_setmixout_handler */

int osc_setmixin_handler(const char *path, const char *types, lo_arg ** argv,
			 int argc, void *data, void *user_data)
{
  int i;
  int sample,channel,to_set;
  fprintf(stdout,"path: <%s>\n", path);
  sample=argv[0]->i;
  channel=argv[1]->i;
  to_set=argv[2]->i;
  ficus_setmixin(sample,channel,to_set);
  return 0;
} /* osc_setmixin_handler */

int osc_jackmonitor_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int out_channel, in_channel, state;
  fprintf(stdout,"path: <%s>\n", path);
  out_channel=argv[0]->i;
  in_channel=argv[1]->i;
  state=argv[2]->i;
  ficus_jackmonitor(out_channel,in_channel,state);
  return 0;
} /* osc_jackmonitor_handler */

int osc_playback_handler(const char *path, const char *types, lo_arg ** argv,
			 int argc, void *data, void *user_data)
{
  int i;
  fprintf(stdout,"path: <%s>\n", path);
  for (i = 0; i < argc; i++) {
    fprintf(stdout,"arg %d '%c' ", i, types[i]);
    fprintf(stdout,"\n");
  }
  int button=0;
  button=argv[0]->i;
  ficus_playback(button);
  return 0;
} /* osc_playback_handler */

int osc_playback_speed_handler(const char *path, const char *types, lo_arg ** argv,
			       int argc, void *data, void *user_data)
{
  int samplenum;
  float speed;
  fprintf(stdout,"path: <%s>\n", path);
  samplenum=argv[0]->i;
  speed=argv[1]->f;
  ficus_playback_speed(samplenum,speed);
  return 0;
} /* osc_playback_speed_handler */

int osc_playback_rampup_handler(const char *path, const char *types, lo_arg ** argv,
				int argc, void *data, void *user_data)
{
  int samplenum;
  float ramp;
  fprintf(stdout,"path: <%s>\n", path);
  samplenum=argv[0]->i;
  ramp=argv[1]->f;
  ficus_playback_rampup(samplenum,ramp);
  return 0;
} /* osc_playback_rampup_handler */

int osc_playback_rampdown_handler(const char *path, const char *types, lo_arg ** argv,
				  int argc, void *data, void *user_data)
{
  int samplenum;
  float ramp;
  fprintf(stdout,"path: <%s>\n", path);
  samplenum=argv[0]->i;
  ramp=argv[1]->f;
  ficus_playback_rampdown(samplenum,ramp);
  return 0;
} /* osc_playback_rampdown_handler */

int osc_capture_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number, seconds;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  seconds=argv[1]->i;
  ficus_capture(bank_number,seconds);
  return 0;
} /* osc_capture_handler */

int osc_capturef_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number, num_frames;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  num_frames=argv[1]->i;
  ficus_capturef(bank_number,num_frames);
  return 0;
} /* osc_capturef_handler */

int osc_durationf_out_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  ficus_durationf_out(bank_number);
  return 0;
} /* osc_durationf_out_handler */

int osc_durationf_in_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  ficus_durationf_out(bank_number);
  return 0;
} /* osc_durationf_in_handler */

int osc_killplayback_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  ficus_killplayback(bank_number);
  return 0;
} /* osc_killplayback_handler */

int osc_killcapture_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  ficus_killcapture(bank_number);
  return 0;
} /* osc_killcapture_handler */

int osc_isplaying_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  ficus_isplaying(bank_number);
  return 0;
} /* osc_isplaying_handler */

int osc_iscapturing_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  ficus_iscapturing(bank_number);
  return 0;
} /* osc_iscapturing_handler */

int osc_islooping_handler(const char *path, const char *types, lo_arg ** argv, 
			    int argc, void *data, void *user_data) {
  int bank_number;
  fprintf(stdout,"path: <%s>\n", path);
  bank_number=argv[0]->i;
  ficus_islooping(bank_number);
  return 0;
} /* osc_islooping_handler */

int quit_candor(monome_t *monome) {
  /* clean-up */
  fprintf(stdout, "Closing monome...\n");
  monome_close(monome);
  fprintf(stdout, "Cleaning up ficus...\n");
  ficus_clean();
  fprintf(stdout, "Flushing stdout...\n");
  fflush(stdout);
  exit(0);
} /* quit_candor */

int osc_quit_handler(const char *path, const char *types, lo_arg ** argv,
		     int argc, void *data, void *user_data)
{
  fprintf(stdout,"quitting\n\n");
    /* clean-up */
  ficus_clean();
  fflush(stdout);
  exit(0);
} /* osc_quit_handler */

/* END OPEN SOUND CONTROL INTERFACE*/

int
print_usage() {
  fprintf(stdout,"Usage: candor [options] [arg]\n\n");
  fprintf(stdout,"Options:\n"
	  " -h,  --help          displays this menu\n"
	  " -p,  --port          set osc interface receiving port. default: 94606\n"
	  " -sp, --send-port     set osc interface sending port. default: 946407\n"
          " -n,  --name          monome device name to connect to. no default\n" 
	  " -m,  --monome        disables serialosc, enables manual monome port configuration. no default\n"
	  " -mi, --midi          enables alsa midi device, ex: 'hw:VSL'\n" 
	  " -b,  --bitdepth      set bitdepth of capture to 8,16,24,32,64, or 128. default: 24\n"
	  " -pa, --path          set directory of where to store captured sounds. default: 'samples/'\n"
	  " -pr, --prefix        set prefix name for all captured sounds. default: 'sample'\n"
	  " -f,  --file          set path of session file to load preexisting sounds.\n\n"
          "documentation available soon\n\n");
  exit(0);

  return 0;
} /* print_usage */

void
print_header(void) {

  printf("\n\n"
	 " welcome to the\n\n"
	 "     _,_   ,  ,  ,_   _, ,_     \n"  
	 "    / '|\\  |\\ |  | \\,/ \\,|_)    \n"
	 "   '\\_ |-\\ |'\\| _|_/'\\_/'| \\    \n"
	 "      `'  `'  `'     '   '  `   \n"
	 "\t\trealtime music system\n");

  printf("\n");

} /* print_header */

void
monome_thread(monome_t *monome)
{
    monome_event_loop(monome);
} /* monome_thread */

int 
main(int argc, char *argv[]) 
{
  int c=0;
  char *store_flag=NULL;
  char *store_input=NULL;
  char *file_path=NULL;
  int bitdepth=0;
  char *rawmidi_device=NULL;
  char *osc_port=NULL;
  char *non_serialosc_port = "init";
  int connchan = 0;
  char monome_device_addr[128];;

  if( argc > 1 )
    if( !strcmp(argv[1],"-h") ||
	!strcmp(argv[1],"--help") ) {
      printf("welcome to the candor realtime music system\n\n");
      print_usage();
      exit(0);
    }    
  print_header();
  
  /* process command-line input */
  for(c=1; c<argc; c++)
    {
      store_flag = argv[c];
      if( store_flag != NULL )
	{
	  if( !strcmp(store_flag,"-p") ||
	      !strcmp(store_flag,"--port")) {
	    store_input=argv[c+1];
	    osc_port=store_input;
	  }

	  if( !strcmp(store_flag,"-sp") ||
	      !strcmp(store_flag,"--send-port")) {
	    store_input=argv[c+1];
	    osc_port_out=store_input;
	  }
	  
	  if( !strcmp(store_flag,"-n") ||
	      !strcmp(store_flag,"--name")) {
	    store_input=argv[c+1];
	    monome_name_user_defined=store_input;
	  }
	  
	  if( !strcmp(store_flag,"-m") ||
	      !strcmp(store_flag,"--monome")) {
	    store_input = argv[c+1];
	    non_serialosc_port=store_input;
	  }

	  if( !strcmp(store_flag,"-mi") ||
	      !strcmp(store_flag,"--midi")) {
	    store_input = argv[c+1];
	    rawmidi_device=store_input;
	  }
	  
	  if( !strcmp(store_flag,"-b") ||
	      !strcmp(store_flag,"--bitdepth")) {
	    store_input = argv[c+1];
	    bitdepth=atoi(store_input);
	  }
	  
	  if( !strcmp(store_flag,"-pa") ||
	      !strcmp(store_flag,"--path")) {
	    store_input = argv[c+1];
	    sampler_path=store_input;
	  }
	  
	  if( !strcmp(store_flag,"-pr") ||
	      !strcmp(store_flag,"--prefix")) {
	    store_input = argv[c+1];
	    sampler_prefix=store_input;
	  }
	  
	  if( !strcmp(store_flag,"-f") ||
	      !strcmp(store_flag,"--file")) {
	    store_input = argv[c+1];
	    file_path=store_input;
	  }

	  if( !strcmp(store_flag,"-cc"))
	    connchan=1;
	  
	  /* reset temporarily stored flag&input */
	  store_input=NULL;
	  store_flag=NULL;
	}
    }

  if( osc_port == NULL )
    osc_port="94606";
  
  if( osc_port_out == NULL )
    osc_port_out="94607";

  if( sampler_path==NULL )
    sampler_path="samples/";
  if( sampler_prefix==NULL )
    sampler_prefix="sample";

  /* if it's not 8,16,24,32,or 64 assign 24 bits as default */
  switch(bitdepth) {
  case 8:
    break;
  case 16:
    break;
  case 24:
    break;
  case 32:
    break;
  case 64:
    break;
  default:
    bitdepth=24;
  }
  
  if( non_serialosc_port == "init" ) {
    /* begin an osc server on port 94606 for serialosc setup */
    lo_server_thread st = lo_server_thread_new( osc_port, error);

    /* add method that will match any path and args */
    /* lo_server_thread_add_method(st, NULL, NULL, generic_handler, NULL); */
    /* add method that will match the path /quit with no args */
    lo_server_thread_add_method(st, "/serialosc/device", "ssi", osc_serialosc_device_handler, NULL);
    lo_server_thread_start(st);

    /* set outgoing address of osc messages */
    lo_address lo_addr;
    lo_addr = lo_address_new("127.0.0.1", "12002");

    lo_address lo_addr_send;
    lo_addr_send = lo_address_new("127.0.0.1",osc_port_out);

    if (lo_send(lo_addr,"/serialosc/list","si","127.0.0.1",(int)strtol(osc_port, (char **)NULL, 10)) == -1 ) {
      fprintf(stderr,"ERROR sending /serialosc/list to serialosc. Aborting!\n");
      exit(1);
    } else {
      /* waits until we find a monome */
      fprintf(stderr, "\nwaiting 5 seconds to discover monome:");
      c = 0;
      while( monome_serialosc_port == 0 ) {
	{
	  if(c>4)
	    {;
	      break;
	    }
	  usleep(1000000);
	  c+=1;
	  fprintf(stderr, ".", c);
	}
      }
      fprintf(stdout, "\n");
    }
    lo_server_thread_free(st);
    snprintf(monome_device_addr,400,"osc.udp://127.0.0.1:%d/monome",monome_serialosc_port);
  } else {
    strcpy(monome_device_addr,"osc.udp://127.0.0.1:");
    strcat(monome_device_addr,non_serialosc_port);
    strcat(monome_device_addr,"/monome");
  }
  
  if( (monome_serialosc_port==0) && (non_serialosc_port=="init") ) {
    fprintf(stderr, "monome not found.. make sure device is connected and that serialosc is running\n\n");
    return 0;
  }

  /* need to declare this here in case we initialize the osc interface's osc_quit_handler */
  monome_t *monome=NULL; 

  /* start a new long-living osc server on port 94606 */
  lo_server_thread st = lo_server_thread_new(osc_port, error);
  /* add method that will match any path and args */
  /* lo_server_thread_add_method(st, NULL, NULL, generic_handler, NULL); */
  /* add method that will match the path /quit with no args */
  lo_server_thread_add_method(st, "/candor/load", "si", osc_load_handler, NULL);
  lo_server_thread_add_method(st, "/candor/loop", "ii", osc_loop_handler, NULL);
  lo_server_thread_add_method(st, "/candor/setmixout", "iii", osc_setmixout_handler, NULL);
  lo_server_thread_add_method(st, "/candor/setmixin", "iii", osc_setmixin_handler, NULL);
  lo_server_thread_add_method(st, "/candor/jackmonitor", "iii", osc_jackmonitor_handler, NULL);
  lo_server_thread_add_method(st, "/candor/playback", "i", osc_playback_handler, NULL);
  lo_server_thread_add_method(st, "/candor/playback_speed", "if", osc_playback_speed_handler, NULL);
  lo_server_thread_add_method(st, "/candor/playback_rampup", "if", osc_playback_rampup_handler, NULL);
  lo_server_thread_add_method(st, "/candor/playback_ramdown", "if", osc_playback_rampdown_handler, NULL);
  lo_server_thread_add_method(st, "/candor/capture", "ii", osc_capture_handler, NULL);
  lo_server_thread_add_method(st, "/candor/capturef", "ii", osc_capturef_handler, NULL);
  lo_server_thread_add_method(st, "/candor/durationf_out", "i", osc_durationf_out_handler, NULL);
  lo_server_thread_add_method(st, "/candor/durationf_in", "i", osc_durationf_in_handler, NULL);
  lo_server_thread_add_method(st, "/candor/killplayback", "i", osc_killplayback_handler, NULL);
  lo_server_thread_add_method(st, "/candor/killcapture", "i", osc_killcapture_handler, NULL);
  lo_server_thread_add_method(st, "/candor/isplaying", "i", osc_isplaying_handler, NULL);
  lo_server_thread_add_method(st, "/candor/iscapturing", "i", osc_iscapturing_handler, NULL);
  lo_server_thread_add_method(st, "/candor/islooping", "i", osc_islooping_handler, NULL);
  lo_server_thread_add_method(st, "/candor/quit", NULL, osc_quit_handler, NULL);
  lo_server_thread_add_method(st, "/candor/clock", NULL, osc_external_clock_handler, NULL);
  lo_server_thread_start(st);
  printf("\nosc receive port: %s\n", osc_port);
  printf("osc send port: %s\n", osc_port_out);
  printf("monome address: %s\n", monome_device_addr);
  printf("midi device: %s\n", rawmidi_device);
  printf("bitdepth: %dbits\n", bitdepth);
  printf("path: %s,  ", sampler_path);
  printf("prefix: %s,  ", sampler_prefix);
  printf("file_path: %s\n\n\n", file_path);

  /* open the monome device */
  if( !(monome = monome_open(monome_device_addr, "8000")) ){
    fprintf(stderr, "ERROR failed to open monome device. Aborting!");
    return -1;
  }
  /* clear monome LEDs */
  monome_led_all(monome, 0);

  /* register our button presses callback for triggering events
   and maintaining state */
  monome_register_handler(monome, MONOME_BUTTON_DOWN, handle_press, NULL);
  monome_register_handler(monome, MONOME_BUTTON_UP, handle_lift, NULL);

  /* candor general setup */
  setup_candor(monome, "candor", sampler_path, sampler_prefix, 24, rawmidi_device);

  if( connchan )
    ficus_connect_channels(8,8);

  /* load files */
  if( file_path!=NULL )
      load_from_file(file_path);

  pthread_t monome_thread_id;
  pthread_create(&monome_thread_id, NULL, monome_thread, monome);
  pthread_detach(&monome_thread_id);

  printf("press <ENTER> to quit\n\n");

  int key = 0;
  while(!key) {
    key = getchar();
    usleep(10000);
  }

  /* clean-up */
  monome_close(monome);
  ficus_clean();

  return 0;
} /* main */
