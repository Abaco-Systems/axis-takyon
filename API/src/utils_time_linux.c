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

#include <time.h>
#include <sched.h>
#include "takyon_private.h"

void clockSleep(int64_t microseconds) {
  if (microseconds >= 0) {
    struct timespec rqtp;
    rqtp.tv_sec = microseconds / 1000000;
    rqtp.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&rqtp, NULL);
  }
}

void clockSleepYield(int64_t microseconds) {
  /*
   * This is used to facilitate waiting with a context switch.
   * This will give other lower priority tasks the ability to
   * get some CPU time.
   */
  if (microseconds > 0) {
    struct timespec rqtp;
    rqtp.tv_sec = microseconds / 1000000;
    rqtp.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&rqtp, NULL);
  } else {
    /* Need to at least do a context switch out to give some other process a try (not a true thread yield) */
    sched_yield();
  }
}

int64_t clockTimeNanoseconds() {
  int64_t total_nanoseconds;
  struct timeval tp;
  gettimeofday(&tp, NULL);
  total_nanoseconds = ((int64_t)tp.tv_sec * 1000000000LL) + (int64_t)(tp.tv_usec * 1000);
  return total_nanoseconds;
}
