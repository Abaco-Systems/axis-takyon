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

#include "takyon_extensions.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <Windows.h>
  #include <sys/timeb.h>
#else
  #include <math.h>
  #include <sys/time.h>
#endif

void takyonSleep(double seconds) {
#ifdef _WIN32
  int milliseconds = (int)(seconds * 1000);
  if (milliseconds > 0) {
    Sleep(milliseconds);
  }
#else
  int int_seconds = (int)floor(seconds);
  int nanosecs = (int)((seconds - int_seconds) * 1000000000);
  if (int_seconds > 0 || nanosecs > 0) {
    struct timespec rqtp;
    rqtp.tv_sec = int_seconds;
    rqtp.tv_nsec = nanosecs;
    nanosleep(&rqtp, NULL);
  }
#endif
}

double takyonTime() {
#ifdef _WIN32
  static long long base_time;
  static int got_base_time = 0;
  static int tested_freq = 0;
  static long long freq = 0L;
  if (!tested_freq) {
    tested_freq = 1;
    if (!QueryPerformanceFrequency((LARGE_INTEGER *)&freq)) {
      // High speed clock not available
      freq = 0L;
    }
  }
  int got_high_speed_clock = 0;
  long long total_nanoseconds;
  if (freq > 0L) {
    // Uses high performance clock, with a frequency of 'freq'
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
      // The high frequency clock may have stopped working mid run, or right from the beginning
      freq = 0L;
      got_high_speed_clock = 0;
    }
  }
  if (!got_high_speed_clock) {
    // Uses the low resolution wall clock
    struct _timeb timebuffer;
    _ftime(&timebuffer);
    total_nanoseconds = (long long)timebuffer.time * (long long)1000000000;
    total_nanoseconds += ((long long)(timebuffer.millitm) * (long long)(1000000));
  }
  if (!got_base_time) {
    base_time = total_nanoseconds;
    got_base_time = 1;
  }
  return (total_nanoseconds - base_time)/1000000000.0;

#else // Linux
  static long long base_time;
  static int got_base_time = 0;
  struct timeval tp;
  gettimeofday(&tp, NULL);
  long long total_nanoseconds = ((long long)tp.tv_sec * 1000000000LL) + (long long)(tp.tv_usec * 1000);
  if (!got_base_time) {
    base_time = total_nanoseconds;
    got_base_time = 1;
  }
  return (total_nanoseconds - base_time)/1000000000.0;
#endif
}
