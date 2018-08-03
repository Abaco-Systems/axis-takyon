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

#include "pthread.h" // Make sure this is included first since it includes windows.h
#include <stdio.h>
#include <sys/timeb.h>

// -----------------------------------------------------------------------------------------
// Threads
// -----------------------------------------------------------------------------------------

static DWORD WINAPI threadFunction(LPVOID lpParam) {
  pthread_t *thread = (pthread_t *)lpParam;
  /*+ need to figure out how to get returned value back to join() thread */
  /*+ void *ret_value = */thread->threadFunction(thread->user_arg);
  thread->ret_val = 0; //*+*/(DWORD)ret_value;
  return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
  if (attr != NULL) { return 1; } // Not yet supported
  thread->threadFunction = start_routine;
  thread->user_arg = arg;
  DWORD thread_id;
  HANDLE thread_handle = CreateThread(NULL,        // default security attributes
                                      0,           // default stack size
                                      (LPTHREAD_START_ROUTINE)threadFunction,
                                      thread,      // user arguments
                                      0,           // default creation flags
                                      &thread_id); // receive thread identifier
  if (thread_handle == NULL) {
    printf("Failed to create thread: error=%d\n", GetLastError());
    return 1;
  }
  thread->thread_id = thread_id;
  thread->thread_handle = thread_handle;
  return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
  DWORD result = WaitForSingleObject(thread.thread_handle, INFINITE);
  if (result != WAIT_OBJECT_0) {
    return 1;
  }
  if (value_ptr != NULL) *value_ptr = thread.ret_val;
  CloseHandle(thread.thread_handle);
  return 0;
}

pthread_t pthread_self(void) {
  pthread_t thread;
  thread.thread_handle = NULL;
  thread.thread_id = GetCurrentThreadId();
  thread.threadFunction = NULL;
  thread.user_arg = NULL;
  thread.ret_val = NULL;
  return thread;
}

// -----------------------------------------------------------------------------------------
// Mutexes
// -----------------------------------------------------------------------------------------

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
  if (attr == NULL) { return 1; }
  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
  if (attr == NULL) { return 1; }
  // Nothing to do since the init function does not allocate any resources
  return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  if (mutex == NULL) { return 1; }
  InitializeCriticalSection(&mutex->critical_section);
  return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
  if (mutex == NULL) { return 1; }
  EnterCriticalSection(&mutex->critical_section);
  return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  if (mutex == NULL) { return 1; }
  LeaveCriticalSection(&mutex->critical_section);
  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  if (mutex == NULL) { return 1; }
  DeleteCriticalSection(&mutex->critical_section);
  return 0;
}

// -----------------------------------------------------------------------------------------
// Conditional variables
// -----------------------------------------------------------------------------------------

int pthread_condattr_init(pthread_condattr_t *attr) {
  if (attr == NULL) { return 1; }
  return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr) {
  if (attr == NULL) { return 1; }
  // Nothing to do since the init function does not allocate any resources
  return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
  if (cond == NULL) { return 1; }
  InitializeConditionVariable(&cond->cond_var);
  return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
  if (cond == NULL) { return 1; }
  WakeConditionVariable(&cond->cond_var);
  return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
  if (cond == NULL) { return 1; }
  WakeAllConditionVariable(&cond->cond_var);
  return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  if (cond == NULL) { return 1; }
  // Assuming mutex is already locked: EnterCriticalSection(&mutex);
  BOOL success = SleepConditionVariableCS(&cond->cond_var, &mutex->critical_section, INFINITE);
  if (!success) return 1;
  return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime) {
  if (cond == NULL) { return 1; }

  // Assuming mutex is already locked: EnterCriticalSection(&mutex);

  // Get the current time since 1970
  struct _timeb curr_sys_time;
  _ftime(&curr_sys_time);

  // Convert abstime (future time) to elapsed time from now
  DWORD curr_milliseconds = (DWORD)curr_sys_time.time * 1000 + (DWORD)curr_sys_time.millitm;
  DWORD future_milliseconds = (DWORD)abstime->tv_sec * 1000 + (DWORD)(abstime->tv_nsec / 1000000);
  DWORD milliseconds = 0;
  if (future_milliseconds > curr_milliseconds) {
    milliseconds = future_milliseconds - curr_milliseconds;
  }

  // Do the timed wait
  BOOL success = SleepConditionVariableCS(&cond->cond_var, &mutex->critical_section, milliseconds);
  if (!success) return 1;

  return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
  if (cond == NULL) { return 1; }
  // Nothing to do since there is no delete function
  return 0;
}

// -----------------------------------------------------------------------------------------
// Init once
// -----------------------------------------------------------------------------------------

typedef struct {
  void(*onceFunction)(void);
} OnceHandle;

static BOOL CALLBACK initOnceFunction(PINIT_ONCE once_control, PVOID user_data, PVOID *lpContext) {
  if (user_data == NULL) { return FALSE; }
  OnceHandle *once_handle = (OnceHandle *)user_data;
  once_handle->onceFunction();
  *lpContext = 0; // Passes back value to InitOnceExecuteOnce(... &lpContext)
  return TRUE;
}

int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
  OnceHandle once_handle;
  once_handle.onceFunction = init_routine;
  PVOID user_data = &once_handle;
  PVOID lpContext;
  BOOL success = InitOnceExecuteOnce(once_control, initOnceFunction, user_data, &lpContext/*set by callback*/);
  if (success) {
    // Can ignore the value passed back by lpContext
    return 0;
  } else {
    // Failed
    return 1;
  }
}
