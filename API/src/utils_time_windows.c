// Copyright 2018 Abaco Systems
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// -----------------------------------------------------------------------------
// Description:
//   Some time based functionality for Unix based platforms.
//     - sleep
//     - yield (forces a context switch out to give other threads a chance to run)
//     - get wall clock time
// -----------------------------------------------------------------------------

#include "takyon_private.h"

void clockSleep(int64_t microseconds) {
  if (microseconds >= 0) {
    int milliseconds = (int)(microseconds / 1000);
    Sleep(milliseconds);
  }
}

void clockSleepYield(int64_t microseconds) {
  /*
   * This is used to facilitate waiting with a context switch.
   * This will give other lower priority tasks the ability to
   * get some CPU time.
   */
  /* Sleep() on Windows supports a granulatity of milliseconds */
  int milliseconds = (int)(microseconds / 1000);
  if (milliseconds <= 0) milliseconds = 1;
  Sleep(milliseconds);
}

int64_t clockTimeNanoseconds() {
  static int tested_freq = 0;
  static long long freq = 0L;
  int got_high_speed_clock = 0;
  if (!tested_freq) {
    tested_freq = 1;
    if (!QueryPerformanceFrequency((LARGE_INTEGER *)&freq)) {
      /* High speed clock not available */
      freq = 0L;
    }
  }

  long long total_nanoseconds = 0;
  if (freq > 0L) {
    /* Uses high performance clock, with a frequency of 'freq' */
    long long count;
    if (QueryPerformanceCounter((LARGE_INTEGER *)&count)) {
      long long seconds;
      long long nanoseconds;
      long long nanoseconds_per_second = 1000000000L;
      got_high_speed_clock = 1;
      seconds = count / freq;
      count = count % freq;
      nanoseconds = (nanoseconds_per_second * count) / freq;
      total_nanoseconds = (seconds * nanoseconds_per_second) + nanoseconds;
    } else {
      /* The high frequency clock may have stopped working mid run, or right from the beginning */
      freq = 0L;
      got_high_speed_clock = 0;
    }
  }
    
  if (!got_high_speed_clock) {
    /* Uses the low resolution wall clock */
    struct _timeb timebuffer;
    long long total_nanoseconds;
    _ftime(&timebuffer);
    total_nanoseconds = (long long)timebuffer.time * (long long)1000000000;
    total_nanoseconds += ((long long)(timebuffer.millitm) * (long long)(1000000));
  }

  return total_nanoseconds;
}
