// Copyright 2018,2020 Abaco Systems
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
#include <stddef.h>

// Takyon version
#define TAKYON_VERSION_MAJOR 1
#define TAKYON_VERSION_MINOR 0
#define TAKYON_VERSION_PATCH 0

// Takyon constants
#define TAKYON_MAX_INTERCONNECT_CHARS 1000  // Max size of text string to define a path's interconnect
#define TAKYON_NO_WAIT                0     // Use as a timeout. Useful value for recv_start_timeout to test if a message has started arriving, if not go do some other work
#define TAKYON_WAIT_FOREVER          -1     // Use as a timeout. Good for communications that are never expected to fail

// Supported verbosities: a set of mask values that can be selectively enabled
#define TAKYON_VERBOSITY_NONE                0    // A convenience to show there is no verbosity
#define TAKYON_VERBOSITY_ERRORS              0x1  // If enabled, print all errors to strerr (otherwise error printing is suppressed)
#define TAKYON_VERBOSITY_CREATE_DESTROY      0x2  // Minimal stdout messages about path creation/destroying
#define TAKYON_VERBOSITY_CREATE_DESTROY_MORE 0x4  // Additional stdout messages about path creation/destroying
#define TAKYON_VERBOSITY_SEND_RECV           0x8  // Minimal stdout messages about sending/receiving on a path
#define TAKYON_VERBOSITY_SEND_RECV_MORE      0x10 // Additional stdout messages about sending/receiving on a path

// Send/recv completion modes
typedef enum {
  TAKYON_BLOCKING = 0,         // Wait for the transfer to finish from the point of view of the caller: either takyonSend() or takyonRecv().
  TAKYON_USE_IS_SEND_FINISHED  // After starting a non-blocking send, must use takyonIsSendFinished() to know when the send is finished and to allow a subsequent call to takyonSend() on the same buffer.
} TakyonCompletionMethod;

typedef struct {
  bool is_endpointA;      // True = side A of the path. False = side B of the path.
  bool is_polling;        // True = Use CPU polling to detect transfer completion. False = Use event driven (allows CPU to sleep) to detect transfer completion.
  bool abort_on_failure;  // True = abort if API were to return false. False = Don't abort on error, but return false instead. If exiting with a return code is needed, then the application can do it explicitly.
  uint64_t verbosity;     // Or the bits of the TAKYON_VERBOSITY_* mask values to define what is printed to stdout and stderr.
  char interconnect[TAKYON_MAX_INTERCONNECT_CHARS];  // Text string the describes the endpoint's interconnect specification.
  // Timeouts
  double path_create_timeout;   // Max time in seconds allowed to create a connection
  double send_start_timeout;    // Max time in seconds allowed to start sending a message (i.e. send the first byte of the message)
  double send_finish_timeout;   // Max time in seconds allowed to finish sending a message (i.e. send all bytes of the message)
  double recv_start_timeout;    // Max time in seconds allowed to start receiving a message (i.e. receive the first byte of the message)
  double recv_finish_timeout;   // Max time in seconds allowed to finish receiving a message (i.e. receive all bytes of the message)
  double path_destroy_timeout;  // Max time in seconds allowed to destroy a connection (gracefully if possible)
  // Transfer completion notification
  TakyonCompletionMethod send_completion_method;  // Notification method to know when a send is finished: can be either TAKYON_BLOCKING or TAKYON_USE_IS_SEND_FINISHED (non blocking)
  TakyonCompletionMethod recv_completion_method;  // Notification method to know when a recv is finished: only TAKYON_BLOCKING is supported.
  // Buffers (i.e. transport memory)
  int nbufs_AtoB;                   // Number of transport buffers from A to B. Must be 0 or greater.
  int nbufs_BtoA;                   // Number of transport buffers from B to A. Must be 0 or greater.
  uint64_t *sender_max_bytes_list;  // List of send buffer sizes. Must be 0 or greater. Each size can be different. Does not need to match remote recver sizes. Set to NULL if 0 buffers.
  uint64_t *recver_max_bytes_list;  // List of recv buffer sizes. Must be 0 or greater. Each size can be different. Does not need to match remote sender sizes. Set to NULL if 0 buffers.
  size_t *sender_addr_list;         // List of pre-allocated send buffer addresses. Set list entry to NULL to allow Takyon to allocate the buffer. Set to NULL if 0 buffers.
  size_t *recver_addr_list;         // List of pre-allocated recv buffer addresses. Set list entry to NULL to allow Takyon to allocate the buffer. Set to NULL if 0 buffers.
  // No need to set or modify the following since Takyon manages it
  char *error_message;  // For returning error messages if a failure occurs with create, send, or recv. This should not be freed by the application.
} TakyonPathAttributes;

typedef struct {
  TakyonPathAttributes attrs; // Contains a copy of the attributes passed in from takyonCreate()
  void *private_path;
} TakyonPath;

#ifdef __cplusplus
extern "C"
{
#endif

extern TakyonPath *takyonCreate(TakyonPathAttributes *attributes);   // If this returns NULL, call free(attributes.error_message) if not NULL, after reading the message
extern bool takyonSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret);
extern bool takyonIsSendFinished(TakyonPath *path, int buffer_index, bool *timed_out_ret);
extern bool takyonRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret);
extern char *takyonDestroy(TakyonPath **path_ret);   // If this returns non NULL, it contains the error message to be printed or stored, and it needs to be freed by the application

#ifdef __cplusplus
}
#endif

#endif
