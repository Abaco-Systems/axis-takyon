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

#include <stdbool.h> // bool
#include <stdint.h>  // uint64_t
#include <stddef.h>  // size_t

// Takyon version
#define TAKYON_VERSION_MAJOR 1
#define TAKYON_VERSION_MINOR 1
#define TAKYON_VERSION_PATCH 0

// Takyon constants
#define TAKYON_MAX_INTERCONNECT_CHARS 1000  // Max size of text string to define a path's interconnect
#define TAKYON_NO_WAIT                0     // Use as a timeout. Useful for recv_start_timeout to test if a message has started arriving
#define TAKYON_WAIT_FOREVER          -1     // Use as a timeout. Useful for communications that are never expected to fail

// Supported verbosities: a set of mask values that can be selectively enabled
#define TAKYON_VERBOSITY_NONE                0x00  // A convenience to show there is no verbosity
#define TAKYON_VERBOSITY_ERRORS              0x01  // If enabled, print all errors to strerr (otherwise error printing is suppressed)
#define TAKYON_VERBOSITY_CREATE_DESTROY      0x02  // Minimal stdout messages about path creation/destroying
#define TAKYON_VERBOSITY_CREATE_DESTROY_MORE 0x04  // Additional stdout messages about path creation/destroying
#define TAKYON_VERBOSITY_SEND_RECV           0x08  // Minimal stdout messages about sending/receiving on a path
#define TAKYON_VERBOSITY_SEND_RECV_MORE      0x10  // Additional stdout messages about sending/receiving on a path

// takyonSend() flags
typedef enum {
  TAKYON_SEND_FLAGS_NONE                = 0x0,  // No flags are set
  TAKYON_SEND_FLAGS_NON_BLOCKING        = 0x1   // Starts send and returns. takyonIsSent() needed to determine if buffer is sent.
  //*+ FUTURE */TAKYON_SEND_FLAGS_NO_SENT_NOTIFCATION = 0x2  // If non blocking, then takyonIsSent() is a no-op. This improves efficiency of RDMA sends
  //*+ FUTURE */TAKYON_SEND_FLAGS_NO_RECV_NOTIFCATION = 0x4  // takyonRecv() should not be called, takyonPostRecv() will be required for the buffer that received it
  //*+ FUTURE: maybe a pull request called on the recv side. Needs more investigation */
} TakyonSendFlagsMask;

// takyonRecv() flags
typedef enum {
  TAKYON_RECV_FLAGS_NONE          = 0x0,  // No flags are set
  TAKYON_RECV_FLAGS_MANUAL_REPOST = 0x1   // After the received buffer is processed, takyonPostRecv() must be called to allow the buffer to be used again.
} TakyonRecvFlagsMask;

typedef struct {
  bool is_endpointA;      // True = side A of the path. False = side B of the path.
  bool is_polling;        // True = Use CPU polling to detect transfer completion. False = Use event driven (allows CPU to sleep) to detect
                          // transfer completion.
  bool abort_on_failure;  // True = abort if API were to return false. False = Don't abort on error, but return false instead. If exiting with a
                          // return code is needed, then the application can do it explicitly.
  uint64_t verbosity;     // Or the bits of the TAKYON_VERBOSITY_* mask values to define what is printed to stdout and stderr.
  char interconnect[TAKYON_MAX_INTERCONNECT_CHARS];  // Text string the describes the endpoint's interconnect specification.

  // Timeouts
  double path_create_timeout;   // Max time in seconds allowed to create a connection
  double send_start_timeout;    // Max time in seconds allowed to start sending a message (i.e. send the first byte of the message)
  double send_finish_timeout;   // Max time in seconds allowed to finish sending a message (i.e. send all bytes of the message)
  double recv_start_timeout;    // Max time in seconds allowed to start receiving a message (i.e. receive the first byte of the message)
  double recv_finish_timeout;   // Max time in seconds allowed to finish receiving a message (i.e. receive all bytes of the message).
                                // This should be non-zero to provide reasonable time to complete the active transfer.
  double path_destroy_timeout;  // Max time in seconds allowed to destroy a connection (gracefully if possible)

  // Buffers (i.e. transport memory)
  int nbufs_AtoB;                   // Number of transport buffers from A to B. Must be 0 or greater.
  int nbufs_BtoA;                   // Number of transport buffers from B to A. Must be 0 or greater.
  uint64_t *sender_max_bytes_list;  // List of send buffer sizes. Must be 0 or greater. Each size can be different.
                                    // Does not need to match remote recver sizes. Set to NULL if 0 buffers.
  uint64_t *recver_max_bytes_list;  // List of recv buffer sizes. Must be 0 or greater. Each size can be different.
                                    // Does not need to match remote sender sizes. Set to NULL if 0 buffers.
  size_t *sender_addr_list;         // List of pre-allocated send buffer addresses. Set list entry to NULL to allow Takyon to allocate
                                    // the buffer. Set to NULL if 0 buffers.
  size_t *recver_addr_list;         // List of pre-allocated recv buffer addresses. Set list entry to NULL to allow Takyon to allocate
                                    // the buffer. Set to NULL if 0 buffers.
  //*+ FUTURE */uint64_t *recver_flags: DONT_PREPOST_RECVS Good if need to RDMA write without needing takyonRecv()?

  // -----------------------------------------------------------------------------------------------------------------
  // No need to set or modify the following since Takyon sets and manages them. View values from takyon_path->attrs...
  // -----------------------------------------------------------------------------------------------------------------
  // Define functionality supported by Takyon interconnect
  bool Send_supported;      // True if takyonSend() is implemented for the specified interconnect
  bool IsSent_supported;    // True if takyonIsSent() is implemented for the specified interconnect
  bool PostRecv_supported;  // True if takyonPostRecv() is implemented for the specified interconnect
  bool Recv_supported;      // True if takyonRecv() is implemented for the specified interconnect
  // Error messages
  char *error_message;      // For returning error messages if a failure occurs with create, send, or recv. This should not be freed
                            // by the application.
} TakyonPathAttributes;

typedef struct {
  TakyonPathAttributes attrs; // Contains a copy of the attributes passed in from takyonCreate()
  void *private_path;
} TakyonPath;

#ifdef __cplusplus
extern "C"
{
#endif

// Create a communication endpoint. If this returns NULL, call free(attributes.error_message) if not NULL, after reading the message
extern TakyonPath *takyonCreate(TakyonPathAttributes *attributes);

// Send a message
extern bool takyonSend(TakyonPath *path, int buffer_index, TakyonSendFlagsMask flags, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret);
// See if a non blocking send has completed. Only allowed by supported interconnects and if send_completion_method == TAKYON_SEND_IS_NON_BLOCKING
extern bool takyonIsSent(TakyonPath *path, int buffer_index, bool *timed_out_ret);

// Wait for a message to arrive
extern bool takyonRecv(TakyonPath *path, int buffer_index, TakyonRecvFlagsMask flags, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret);
// Allow a recv buffer to be used again. All recv buffers are pre posted by takyonCreate()
extern bool takyonPostRecv(TakyonPath *path, int buffer_index);

// Destroy a communication endpoint.
// If this returns non NULL, it contains the error message to be printed or stored, and it needs to be freed by the application.
extern char *takyonDestroy(TakyonPath **path_ret);



// Potential functions/extensions:
//
//
// -----------------------------------------------------------------------------
// Modify send/recv to allow strided and SGE (scatter gather elements) transfers
// -----------------------------------------------------------------------------
//     typedef struct {
//       // One or both of nsges and nblocks must be zero
//       uint64_t nsges;    // Error if >0 and interconnect does not support SGEs. RDMA can support a small number of SGEs
//       uint64_t nblocks;  // 1 block if contiguous (error if interconnect does not support > 1). Very rare to find interconnects that will support srided
//       // SGE Only
//       TakyonSge *sge_list;
//       // Contiguous (nblocks==1) or strided only (nblocks>1)
//       uint64_t bytes_per_block;
//       uint64_t src_offset;
//       uint64_t src_stride;   // only used if nblocks > 1
//       uint64_t dest_offset;
//       uint64_t dest_stride;  // only used if nblocks > 1
//     } TakyonSendLayout;
//     bool takyonSend(TakyonPath *path, int buffer_index, TakyonSendFlagsMask flags, TakyonSendLayout *layout, bool *timed_out_ret);
//
//     typedef struct {
//       // One or both of nsges and nblocks must be zero
//       uint64_t nsges;    // Error if >0 and interconnect does not support SGEs. RDMA can support a small number of SGEs
//       uint64_t nblocks;  // 1 block if contiguous (error if interconnect does not support > 1). Very rare to find interconnects that will support srided
//       // SGE Only
//       TakyonSge *sge_list;
//       // Contiguous (nblocks==1) or strided only (nblocks>1)
//       uint64_t bytes_per_block;
//       uint64_t offset;
//       uint64_t stride;   // only relevant if nblocks > 1
//     } TakyonRecvLayout;
//     bool takyonRecv(TakyonPath *path, int buffer_index, TakyonRecvFlagsMask flags, TakyonRecvLayout *layout, bool *timed_out_ret);
//
//
// -----------------------------------------------------------------------
// Publish/Subscribe? (a potential replacement for the overly complex DDS)
// -----------------------------------------------------------------------
//     Simplify DDS concepts. Still have partiticapnts, publishers, and subscribers, but messages are opaque (removes need for intermediate language).

#ifdef __cplusplus
}
#endif

#endif
