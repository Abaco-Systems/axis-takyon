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

#ifndef _takyon_private_h_
#define _takyon_private_h_

#include "takyon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef WITH_CUDA
  #include <cuda_runtime.h>
#endif

typedef struct {
  // Convenience: timeouts converted to nanoseconds (since many interfaces use this instead of double)
  // NOTE: Avoid using an unsigned integer so negative can mean "wait forever" via TAKYON_WAIT_FOREVER
  int64_t path_create_timeout_ns;
  int64_t send_start_timeout_ns;
  int64_t send_finish_timeout_ns;
  int64_t recv_start_timeout_ns;
  int64_t recv_finish_timeout_ns;
  int64_t path_destroy_timeout_ns;

  // Virtual functions that are resolved from a shared object loaded at runtime
  bool (*tknCreate)(TakyonPath *path);
  bool (*tknSend)(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret);
  bool (*tknIsSendFinished)(TakyonPath *path, int buffer_index, bool *timed_out_ret);
  bool (*tknRecv)(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret);
  bool (*tknDestroy)(TakyonPath *path);

  // For each interconnect's specific values
  void *private_data;
} TakyonPrivatePath;

// Helpful global functionality

#define NANOSECONDS_PER_SECOND_DOUBLE 1000000000.0
#define MAX_FILENAME_CHARS 1024                     // Max chars in a shared library filename (limit imposed by a typical OS)
#define MAX_INTERCONNECT_MODULES 20                 // The number of unique interconnect modules that can be dynamically loaded at once (increase as needed)
#define MAX_MMAP_NAME_CHARS 31                      // This small value of 31 is imposed by Apple's OSX
#define MAX_ERROR_MESSAGE_CHARS 10000               // This will hold the error stack message. Try to be terse!
#define MICROSECONDS_TO_SLEEP_BEFORE_DISCONNECTING 10000 // 10 Milliseconds

#ifdef BUILD_STATIC_LIB
#define GLOBAL_VISIBILITY static
#else
#define GLOBAL_VISIBILITY
#endif

#ifdef _WIN32
#include <winsock2.h>
#define TakyonSocket SOCKET // This is implicitly an unsigned int (which is why error checking is different)
#else
#define TakyonSocket int
#endif

#define TAKYON_RECORD_ERROR(msg_ptr, ...) buildErrorMessage(msg_ptr, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)

typedef struct { // This is used to allow threads in the same process to share a mutex and conditional variable for synchronizing communications
  uint32_t interconnect_id; // Make sure this is different for each interconnect that uses this manager.
  uint32_t path_id;
  TakyonPath *pathA;
  TakyonPath *pathB;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool connected;
  bool disconnected;
  bool connection_broken;
  int usage_count;
} InterThreadManagerItem;

typedef struct _MmapHandle *MmapHandle; // Opaque to hide the details, but manages shared memory between processes

#ifdef __cplusplus
extern "C"
{
#endif

// Error recording
extern void buildErrorMessage(char *error_message, const char *file, const char *function, int line_number, const char *format, ...);

// Endian
extern bool endianIsBig();
extern void endianSwapUInt16(uint16_t *data, uint64_t num_elements);
extern void endianSwapUInt32(uint32_t *data, uint64_t num_elements);

// Argument parsing
extern bool argGetInterconnect(const char *arguments, char *result, const int max_chars, char *error_message);
extern bool argGetFlag(const char *arguments, const char *name);
extern bool argGetText(const char *arguments, const char *name, char *result, const int max_chars, bool *found_ret, char *error_message);
extern bool argGetInt(const char *arguments, const char *name, int *result, bool *found_ret, char *error_message);
extern bool argGetUInt(const char *arguments, const char *name, uint32_t *result, bool *found_ret, char *error_message);
extern bool argGetFloat(const char *arguments, const char *name, float *result, bool *found_ret, char *error_message);

// Shared libraries
extern bool sharedLibraryLoad(const char *interconnect_module, int is_verbose, char *error_message);
extern bool sharedLibraryGetInterconnectFunctionPointers(const char *interconnect_module, TakyonPrivatePath *private_path, char *error_message);
extern bool sharedLibraryUnload(const char *interconnect_module, char *error_message);

// Local memory allocation (can be shared between threads but not processes)
extern int memoryPageSize();
extern bool memoryAlloc(size_t alignment, size_t size, void **addr_ret, char *error_message);
extern bool memoryFree(void *addr, char *error_message);
#ifdef WITH_CUDA
extern void *cudaMemoryAlloc(int cuda_device_id, uint64_t bytes, char *error_message);
extern bool cudaMemoryFree(void *addr, char *error_message);
#endif

// Memory mapped allocation (can be shared between processes)
extern bool mmapAlloc(const char *map_name, uint64_t bytes, void **addr_ret, MmapHandle *mmap_handle_ret, char *error_message);
extern bool mmapGet(const char *map_name, uint64_t bytes, void **addr_ret, bool *got_it_ret, MmapHandle *mmap_handle_ret, char *error_message);
extern bool mmapGetTimed(const char *mmap_name, uint64_t bytes, void **addr_ret, MmapHandle *mmap_handle_ret, int64_t timeout_ns, bool *timed_out_ret, char *error_message);
extern bool mmapFree(MmapHandle mmap_handle, char *error_message);
#ifdef WITH_CUDA
// Check if local or remote addr is CUDA or CPU
extern bool isCudaAddress(void *addr, bool *is_cuda_addr_ret, char *error_message);
// Sharing a CUDA address
extern bool cudaCreateIpcMapFromLocalAddr(void *cuda_addr, cudaIpcMemHandle_t *ipc_map_ret, char *error_message);
extern void *cudaGetRemoteAddrFromIpcMap(cudaIpcMemHandle_t remote_ipc_map, char *error_message);
extern bool cudaReleaseRemoteIpcMappedAddr(void *mapped_cuda_addr, char *error_message);
// CUDA events need to synchronize cudaMemcpy() between processes
extern bool cudaEventAlloc(int cuda_device_id, cudaEvent_t *event_ret, char *error_message);
extern bool cudaEventFree(cudaEvent_t *event, char *error_message);
extern bool cudaCreateIpcMapFromLocalEvent(cudaEvent_t *event, cudaIpcEventHandle_t *ipc_map_ret, char *error_message);
extern bool cudaGetRemoteEventFromIpcMap(cudaIpcEventHandle_t remote_ipc_map, cudaEvent_t *remote_event_ret, char *error_message);
extern bool cudaEventNotify(cudaEvent_t *event, char *error_message);
extern bool cudaEventWait(cudaEvent_t *event, char *error_message);
#endif

// Thread Utils
extern bool threadCondWait(pthread_mutex_t *mutex, pthread_cond_t *cond_var, int64_t timeout_ns, bool *timed_out_ret, char *error_message);

// Time
extern void clockSleep(int64_t microseconds);
extern void clockSleepYield(int64_t microseconds);
extern int64_t clockTimeNanoseconds();

// Connected (reliable) sockets
extern bool socketCreateLocalClient(const char *socket_name, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message);
extern bool socketCreateTcpClient(const char *ip_addr, uint16_t port_number, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message);
extern bool socketCreateLocalServer(const char *socket_name, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message);
extern bool socketCreateTcpServer(const char *ip_addr, uint16_t port_number, bool allow_reuse, TakyonSocket *socket_fd_ret, int64_t timeout_ns, char *error_message);
extern bool socketCreateEphemeralTcpClient(const char *ip_addr, const char *interconnect_name, uint32_t path_id, TakyonSocket *socket_fd_ret, int64_t timeout_ns, uint64_t verbosity, char *error_message);
extern bool socketCreateEphemeralTcpServer(const char *ip_addr, const char *interconnect_name, uint32_t path_id, TakyonSocket *socket_fd_ret, int64_t timeout_ns, uint64_t verbosity, char *error_message);
extern bool socketSend(TakyonSocket socket_fd, void *addr, size_t bytes_to_write, bool is_polling, int64_t start_timeout_ns, int64_t finish_timeout_ns, bool *timed_out_ret, char *error_message);
extern bool socketRecv(TakyonSocket socket_fd, void *data_ptr, size_t bytes_to_read, bool is_polling, int64_t start_timeout_ns, int64_t finish_timeout_ns, bool *timed_out_ret, char *error_message);

// Connectionless sockets (unreliable and may drop data)
extern bool socketCreateUnicastSender(const char *ip_addr, uint16_t port_number, TakyonSocket *socket_fd_ret, void **sock_in_addr_ret, char *error_message);
extern bool socketCreateUnicastReceiver(const char *ip_addr, uint16_t port_number, bool allow_reuse, TakyonSocket *socket_fd_ret, char *error_message);
extern bool socketCreateMulticastSender(const char *ip_addr, const char *multicast_group, uint16_t port_number, bool disable_loopback, int ttl_level, TakyonSocket *socket_fd_ret, void **sock_in_addr_ret, char *error_message);
extern bool socketCreateMulticastReceiver(const char *ip_addr, const char *multicast_group, uint16_t port_number, bool allow_reuse, TakyonSocket *socket_fd_ret, char *error_message);
extern bool socketDatagramSend(TakyonSocket socket_fd, void *sock_in_addr, void *addr, size_t bytes_to_write, bool is_polling, int64_t timeout_ns, bool *timed_out_ret, char *error_message);
extern bool socketDatagramRecv(TakyonSocket socket_fd, void *data_ptr, size_t buffer_bytes, uint64_t *bytes_read_ret, bool is_polling, int64_t timeout_ns, bool *timed_out_ret, char *error_message);

// Helpful socket functions
extern bool socketSetBlocking(TakyonSocket socket_fd, bool is_blocking, char *error_message);
extern bool socketWaitForDisconnectActivity(TakyonSocket socket_fd, int read_pipe_fd, bool *got_socket_activity_ret, char *error_message);
extern bool socketBarrier(bool is_client, TakyonSocket socket_fd, int barrier_id, int64_t timeout_ns, char *error_message);
extern bool socketSwapAndVerifyInt(TakyonSocket socket_fd, int value, int64_t timeout_ns, char *error_message);
extern bool socketSwapAndVerifyUInt64(TakyonSocket socket_fd, uint64_t value, int64_t timeout_ns, char *error_message);
extern bool socketSendUInt64(TakyonSocket socket_fd, uint64_t value, int64_t timeout_ns, char *error_message);
extern bool socketRecvUInt64(TakyonSocket socket_fd, uint64_t *value_ret, int64_t timeout_ns, char *error_message);
extern void socketClose(TakyonSocket socket_fd);

// Ephemeral port manager (let OS find unused port numbers to be used by IP connections: sockets, RDMA)
extern void ephemeralPortManagerSet(const char *interconnect_name, uint32_t path_id, uint16_t ephemeral_port_number, uint64_t verbosity);
extern uint16_t ephemeralPortManagerGet(const char *interconnect_name, uint32_t path_id, int64_t timeout_ns, bool *timed_out_ret, uint64_t verbosity, char *error_message);
extern void ephemeralPortManagerRemove(const char *interconnect_name, uint32_t path_id, uint16_t ephemeral_port_number);

// Pipes (helpful with closing sockets that are sleeping while waiting for activity)
extern bool pipeWakeUpSelect(int read_pipe_fd, int write_pipe_fd, char *error_message);
extern bool pipeCreate(int *read_pipe_fd_ret, int *write_pipe_fd_ret, char *error_message);
extern void pipeDestroy(int read_pipe_fd, int write_pipe_fd);

// Inter-thread manager (organizes communications between threads in the same process)
extern bool interThreadManagerInit();
extern InterThreadManagerItem *interThreadManagerConnect(uint32_t interconnect_id, uint32_t path_id, TakyonPath *path);
extern void interThreadManagerMarkConnectionAsBad(InterThreadManagerItem *item);
extern bool interThreadManagerDisconnect(TakyonPath *path, InterThreadManagerItem *item);

#ifdef BUILD_STATIC_LIB
// These prototypes are only used if statically linking interconnectes into the main takyon library
extern void setInterThreadMemcpyFunctionPointers(TakyonPrivatePath *private_path);
extern void setInterThreadPointerFunctionPointers(TakyonPrivatePath *private_path);
extern void setInterProcessMemcpyFunctionPointers(TakyonPrivatePath *private_path);
extern void setInterProcessPointerFunctionPointers(TakyonPrivatePath *private_path);
extern void setInterProcessSocketFunctionPointers(TakyonPrivatePath *private_path);
extern void setSocketFunctionPointers(TakyonPrivatePath *private_path);
extern void setOneSidedSocketFunctionPointers(TakyonPrivatePath *private_path);
extern void setUnicastSendSocketFunctionPointers(TakyonPrivatePath *private_path);
extern void setUnicastRecvSocketFunctionPointers(TakyonPrivatePath *private_path);
extern void setMulticastSendSocketFunctionPointers(TakyonPrivatePath *private_path);
extern void setMulticastRecvSocketFunctionPointers(TakyonPrivatePath *private_path);
#ifdef WITH_RDMA
extern void setRdmaFunctionPointers(TakyonPrivatePath *private_path);
extern void setUnicastSendRdmaFunctionPointers(TakyonPrivatePath *private_path);
extern void setUnicastRecvRdmaFunctionPointers(TakyonPrivatePath *private_path);
extern void setMulticastSendRdmaFunctionPointers(TakyonPrivatePath *private_path);
extern void setMulticastRecvRdmaFunctionPointers(TakyonPrivatePath *private_path);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
