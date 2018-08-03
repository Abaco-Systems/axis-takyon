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

#ifndef _takyon_private_h_
#define _takyon_private_h_

#include "takyon.h"

typedef struct {
  // Convenince: timeouts converted to nanoseconds (since many interfaces use this instead of double)
  int64_t create_timeout_ns;
  int64_t send_start_timeout_ns;
  int64_t send_complete_timeout_ns;
  int64_t recv_start_timeout_ns;
  int64_t recv_complete_timeout_ns;
  int64_t destroy_timeout_ns;

  // Virtual functions that are resolved from a shared object loaded at runtime
  bool (*tknCreate)(TakyonPath *path);
  bool (*tknSend)(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret);
  bool (*tknSendStrided)(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret);
  bool (*tknSendTest)(TakyonPath *path, int buffer_index, bool *timed_out_ret);
  bool (*tknRecv)(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret);
  bool (*tknRecvStrided)(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret);
  bool (*tknDestroy)(TakyonPath *path);

  // For each interconnect's specific values
  void *private;
} TakyonPrivatePath;

// Helpful global functionality

#define NANOSECONDS_PER_SECOND_DOUBLE 1000000000.0
#define MEMCPY_INTERCONNECT_ID 23                   // This must be different from the other interconnects that use the thread manager
#define POINTER_INTERCONNECT_ID 24                  // This must be different from the other interconnects that use the thread manager
#define MAX_FILENAME_CHARS 1024                     // Max chars in a shared library filename
#define MAX_INTERCONNECT_MODULES 10                 // The number of unique interconnect modules that can be dynamically loaded at once
#define MAX_MMAP_NAME_CHARS 31                      // This small value of 31 is imposed by Apple's OSX
#define MAX_ERROR_MESSAGE_CHARS 10000               // This will hold the error stack message

#ifdef _WIN32
#include <winsock2.h>
#define TakyonSocket SOCKET
#else
#define TakyonSocket int
#endif

#define TAKYON_RECORD_ERROR(msg_ptr, ...) buildErrorMessage(msg_ptr, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

typedef struct {
  int interconnect_id; // Make sure different for memcpy versus pointer shared, etc.
  int path_id;
  TakyonPath *pathA;
  TakyonPath *pathB;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool connected;
  bool disconnected;
  bool connection_broken;
  bool memory_buffers_syned;
  int usage_count;
} ThreadManagerItem;

typedef struct _MmapHandle *MmapHandle;

#ifdef __cplusplus
extern "C"
{
#endif

// Error recording
extern void buildErrorMessage(char *error_message, const char *file, const char *function, int line_number, const char *format, ...);

// Endian
extern bool endianIsBig();

// Argument parsing
extern bool argGet(const char *arguments, const int index, char *result, const int max_chars, char *error_message);
extern bool argGetFlag(const char *arguments, const char *name);
extern bool argGetText(const char *arguments, const char *name, char *result, const int max_chars, bool *found_ret, char *error_message);
extern bool argGetInt(const char *arguments, const char *name, int *result, bool *found_ret, char *error_message);
extern bool argGetFloat(const char *arguments, const char *name, float *result, bool *found_ret, char *error_message);

// Shared libraries
extern bool sharedLibraryLoad(const char *interconnect_module, int is_verbose, char *error_message);
extern bool sharedLibraryGetInterconnectFunctionPointers(const char *interconnect_module, TakyonPrivatePath *private_path, char *error_message);
extern bool sharedLibraryUnload(const char *interconnect_module, char *error_message);

// Local memory allocation
extern int memoryPageSize();
extern bool memoryAlloc(size_t alignment, size_t size, void **addr_ret, char *error_message);
extern bool memoryFree(void *addr, char *error_message);

// Memory mapped allocation
extern bool mmapAlloc(const char *map_name, uint64_t bytes, void **addr_ret, MmapHandle *mmap_handle_ret, char *error_message);
extern bool mmapGet(const char *map_name, uint64_t bytes, void **addr_ret, bool *got_it_ret, MmapHandle *mmap_handle_ret, char *error_message);
extern bool mmapGetTimed(const char *mmap_name, uint64_t bytes, void **addr_ret, MmapHandle *mmap_handle_ret, int64_t timeout_ns, bool *timed_out_ret, char *error_message);
extern bool mmapFree(MmapHandle mmap_handle, char *error_message);

// Threads
extern bool threadCondWait(pthread_mutex_t *mutex, pthread_cond_t *cond_var, int64_t timeout_ns, bool *timed_out_ret, char *error_message);
extern bool threadManagerInit();
extern ThreadManagerItem *threadManagerConnect(int interconnect_id, int path_id, TakyonPath *path);
extern void threadManagerMarkConnectionAsBad(ThreadManagerItem *item);
extern bool threadManagerDisconnect(TakyonPath *path, ThreadManagerItem *item);

// Time
extern void clockSleep(int64_t microseconds);
extern void clockSleepYield(int64_t microseconds);
extern int64_t clockTimeNanoseconds();

// Sockets
extern bool socketCreateLocalClient(const char *socket_name, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message);
extern bool socketCreateTcpClient(const char *ip_addr, uint16_t port_number, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message);
extern bool socketCreateLocalServer(const char *socket_name, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message);
extern bool socketCreateTcpServer(const char *ip_addr, uint16_t port_number, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message);
extern bool socketSetBlocking(TakyonSocket socket_fd, bool is_blocking, char *error_message);
extern bool socketSend(TakyonSocket socket_fd, void *addr, size_t bytes_to_write, bool is_polling, int64_t timeout_ns, bool *timed_out_ret, char *error_message);
extern bool socketRecv(TakyonSocket socket_fd, void *data_ptr, size_t bytes_to_read, bool is_polling, int64_t timeout_ns, bool *timed_out_ret, char *error_message);
extern bool socketWaitForDisconnectActivity(TakyonSocket socket_fd, int read_pipe_fd, bool *got_socket_activity_ret, char *error_message);
extern bool socketBarrier(bool is_client, TakyonSocket socket_fd, int barrier_id, int64_t timeout_ns, char *error_message);
extern bool socketSwapAndVerifyInt(TakyonSocket socket_fd, int value, int64_t timeout_ns, char *error_message);
extern bool socketSwapAndVerifyUInt64(TakyonSocket socket_fd, uint64_t value, int64_t timeout_ns, char *error_message);
extern bool socketSendUInt64(TakyonSocket socket_fd, uint64_t value, int64_t timeout_ns, char *error_message);
extern bool socketRecvUInt64(TakyonSocket socket_fd, uint64_t *value_ret, int64_t timeout_ns, char *error_message);
extern void socketClose(TakyonSocket socket_fd);

// Pipes
extern bool pipeWakeUpSelect(int read_pipe_fd, int write_pipe_fd, char *error_message);
extern bool pipeCreate(const char *pipe_name, int *read_pipe_fd_ret, int *write_pipe_fd_ret, char *error_message);
extern void pipeDestroy(const char *pipe_name, int read_pipe_fd, int write_pipe_fd);

#ifdef _WIN32
extern void setMemcpyFunctionPointers(TakyonPrivatePath *private_path);
extern void setSocketFunctionPointers(TakyonPrivatePath *private_path);
extern void setMmapFunctionPointers(TakyonPrivatePath *private_path);
#endif

#ifdef __cplusplus
}
#endif

#endif
