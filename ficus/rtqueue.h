/* rtqueue.h
This file is a part of 'rtqueue'
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

'rtqueue' is a simple FIFO linked list intended to hold 
JACK sample data for process()'ing.

Copyright 2014 murray foster */

#ifndef rtqueue_h__
#define rtqueue_h__

typedef struct queue
{
  int head;
  int tail;
  int recordlimit;
  int records;
  float *queue;
} rtqueue_t;

rtqueue_t *rtqueue_init(int recordlimit);

int rtqueue_numrecords(rtqueue_t *rtq);

int rtqueue_isfull(rtqueue_t *rtq);

int rtqueue_isempty(rtqueue_t *rtq);

int rtqueue_enq(rtqueue_t *rtq, float data);

float rtqueue_deq(rtqueue_t *rtq);

#endif
