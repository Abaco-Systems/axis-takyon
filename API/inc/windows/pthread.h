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
//   This is only a partial implementation of the pthread standard for Windows.
//   It's enough functionality to allow this Takyon implementation to build
//   and run.
// -----------------------------------------------------------------------------

#ifndef _pthread_windows_h_
#define _pthread_windows_h_

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdbool.h>

#define pthread_once_t INIT_ONCE
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT

// NOTE: use pthread_once() to init mutexes or cond vars instead of using PTHREAD_MUTEX_INITIALIZER or PTHREAD_COND_INITIALIZER

typedef struct {
  HANDLE thread_handle;
  DWORD thread_id;
  void *(*threadFunction)(void *);
  void *user_arg;
  void *ret_val;
} pthread_t;

typedef struct {
  int dummy; // This structure is not yet used
} pthread_attr_t;

typedef struct {
  int dummy; // This structure is not yet used
} pthread_mutexattr_t;

typedef struct {
  // For inter-thread
  CRITICAL_SECTION critical_section;
} pthread_mutex_t;

typedef struct {
  int dummy; // This structure is not yet used
} pthread_condattr_t;

typedef struct {
  // For inter-thread
  CONDITION_VARIABLE cond_var;
} pthread_cond_t;

struct timespec {
  unsigned tv_sec;
  unsigned tv_nsec;
};

#ifdef __cplusplus
extern "C"
{
#endif

// Threads
extern int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
extern int pthread_join(pthread_t thread, void **value_ptr);
extern pthread_t pthread_self(void);

// Mutexes
extern int pthread_mutexattr_init(pthread_mutexattr_t *attr);
extern int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
extern int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
extern int pthread_mutex_lock(pthread_mutex_t *mutex);
extern int pthread_mutex_unlock(pthread_mutex_t *mutex);
extern int pthread_mutex_destroy(pthread_mutex_t *mutex);

// Conditional variables
extern int pthread_condattr_init(pthread_condattr_t *attr);
extern int pthread_condattr_destroy(pthread_condattr_t *attr);
extern int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
extern int pthread_cond_signal(pthread_cond_t *cond);
extern int pthread_cond_broadcast(pthread_cond_t *cond);
extern int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
extern int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);
extern int pthread_cond_destroy(pthread_cond_t *cond);

// Init once
extern int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

#ifdef __cplusplus
}
#endif

#endif
