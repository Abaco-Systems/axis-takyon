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

#ifndef _takyon_h_
#define _takyon_h_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <Windows.h>
  #include <sys/timeb.h>
#else
  #include <unistd.h>
  #include <dlfcn.h>
  #include <sys/time.h>
#endif

#define MAX_TAKYON_INTERCONNECT_CHARS 1000
#define TAKYON_NO_WAIT                0
#define TAKYON_WAIT_FOREVER          -1

// Supported verbosities
#define TAKYON_VERBOSITY_NONE             0    // A convenience to show there is no verbosity
#define TAKYON_VERBOSITY_ERRORS           0x1  // If enabled, print all errors to strerr (otherwise error printing is suppressed)
#define TAKYON_VERBOSITY_INIT             0x2
#define TAKYON_VERBOSITY_INIT_DETAILS     0x4
#define TAKYON_VERBOSITY_RUNTIME          0x8
#define TAKYON_VERBOSITY_RUNTIME_DETAILS  0x10

typedef enum {
  TAKYON_BLOCKING = 0,        // Wait for the transfer to complete when calling takyonSend(), takyonSendStrided() or takyonRecv().
  //TAKYON_NO_NOTIFICATION,     // For sending: start the transfer, and don't ask for notification of send completion. For receiving, don't call takyonRecv().
  TAKYON_USE_SEND_TEST        // After starting a send, must use takyonSendTest() to know when the send is complete.
} TakyonCompletionMethod;

typedef struct {
  bool is_endpointA;         // 1 - side A of the path, 0 - side B of the path.
  bool is_polling;           // Wait completion based on: 1 - CPU polling, 0 - Event driven (allows sleeping)
  bool abort_on_failure;     // 1 - abort if API were to return false, 0 - Don't abort on error, but return false instead, If exit is needed, then application can do it explicitly
  uint64_t verbosity;        // Or the bits of the TAKYON_VERBOSITY_* values
  char interconnect[MAX_TAKYON_INTERCONNECT_CHARS];
  // Timeouts
  double create_timeout;        // Max time to create the connection
  double send_start_timeout;    // Max time to start sending a message
  double send_complete_timeout; // Max time to complete the send transfer
  double recv_start_timeout;    // Max time to start receiving a message
  double recv_complete_timeout; // Max time to complete the recv transfer
  double destroy_timeout;       // Max time to gracefully shut the connection down
  // Transfer completion notification
  TakyonCompletionMethod send_completion_method;  // Notification method to know when a send is complete
  TakyonCompletionMethod recv_completion_method;  // Notification method to know when a recv is complete
  // Buffers
  int nbufs_AtoB;                 // Number of buffers from A to B (defines the list size for the next 4 variables below): Must be 1 or more
  int nbufs_BtoA;                 // Number of buffers from B to A (defines the list size for the next 4 variables below): Must be 1 or more
  uint64_t *sender_max_bytes_list;  // List of sender sizes to register. Must be 0 or greater. Each size can be different. Does not need to match remote recver sizes.
  uint64_t *recver_max_bytes_list;  // List of recver sizes to register. Must be 0 or greater. Each size can be different. Does not need to match remote sender sizes.
  size_t *sender_addr_list;       // List of sender side pre-allocated buffer addresses. Set entry to NULL to auto-allocate.
  size_t *recver_addr_list;       // List of recver side pre-allocated buffer addresses. Set entry to NULL to auto-allocate.
  // No need to modify the following
  char *error_message;                    // For returning error messages if a failure occurs with create, send, or recv. This should not be freed by the application.
} TakyonPathAttributes;

typedef struct {
  TakyonPathAttributes attrs;
  void *private;
} TakyonPath;

#ifdef __cplusplus
extern "C"
{
#endif

extern TakyonPath *takyonCreate(TakyonPathAttributes *attributes);
extern bool takyonSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret);
extern bool takyonSendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret);
extern bool takyonRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret);
extern char *takyonDestroy(TakyonPath **path_ret); // If returns non NULL, then it needs to be freed

// Provisional
extern bool takyonSendStrided(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret);
extern bool takyonRecvStrided(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret);

#ifdef __cplusplus
}
#endif

#endif
