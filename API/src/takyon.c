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

// -----------------------------------------------------------------------------
// Description:
//   This is the core of Takyon; i.e. the glue between the application and the
//   interconnects that Takyon supports
// -----------------------------------------------------------------------------

#include "takyon_private.h"
#include <stdarg.h>

static void checkFailureReportingMethod(TakyonPathAttributes *attributes, const char *function) {
  // An error occured, so need to see how to handle it
  bool print_errors = (attributes->verbosity & TAKYON_VERBOSITY_ERRORS);
  if (attributes->abort_on_failure || print_errors) {
    fflush(stdout);
    if (print_errors) {
      fprintf(stderr, "ERROR in %s(), path=%s:'%s'\n", function, attributes->is_endpointA ? "A" : "B", attributes->interconnect);
    }
    if (print_errors && (attributes->error_message != NULL)) {
      fprintf(stderr, "%s", attributes->error_message);
    }
    fflush(stderr);
    if (attributes->abort_on_failure) abort();
  }
  // Status will be returned
}

static void clearErrorMessage(bool is_endpointA, char *error_message) {
  error_message[0] = '\0';
}

static pthread_once_t L_once_control = PTHREAD_ONCE_INIT;
static pthread_mutex_t L_error_mutex;
static void initErrorMutexOnce(void) {
  pthread_mutex_init(&L_error_mutex, NULL);
}

void buildErrorMessage(char *error_message, const char *file, const char *function, int line_number, const char *format, ...) {
  pthread_once(&L_once_control, initErrorMutexOnce);
  va_list arg_ptr;
  pthread_mutex_lock(&L_error_mutex);
  // Create the message prefix
  size_t offset = strlen(error_message);
  snprintf(error_message+offset, MAX_ERROR_MESSAGE_CHARS-offset, "  %s:%s():line=%d ", file, function, line_number);
  offset = strlen(error_message);
  // Add the error message
  va_start(arg_ptr, format);
  vsnprintf(error_message+offset, MAX_ERROR_MESSAGE_CHARS-offset, format, arg_ptr);
  va_end(arg_ptr);
  // Done
  pthread_mutex_unlock(&L_error_mutex);
}

TakyonPath *takyonCreate(TakyonPathAttributes *attributes) {
  if (attributes == NULL) {
    fprintf(stderr, "ERROR in %s(): attributes is NULL\n", __FUNCTION__);
    abort();
  }
  // IMPORTANT: The following will also need to be validate with each interconnect since some will have different requirements
  if (attributes->nbufs_AtoB < 0) {
    fprintf(stderr, "ERROR in %s(): attributes->nbufs_AtoB can not be negative\n", __FUNCTION__);
    abort();
  }
  if (attributes->nbufs_BtoA < 0) {
    fprintf(stderr, "ERROR in %s(): attributes->nbufs_BtoA can not be negative\n", __FUNCTION__);
    abort();
  }
  if (attributes->nbufs_AtoB == 0 && attributes->nbufs_BtoA == 0) {
    fprintf(stderr, "ERROR in %s(): One of attributes->nbufs_AtoB or attributes->nbufs_BtoA must have at least 1 buffer\n", __FUNCTION__);
    abort();
  }
  int nbufs_sender = attributes->is_endpointA ? attributes->nbufs_AtoB : attributes->nbufs_BtoA;
  int nbufs_recver = attributes->is_endpointA ? attributes->nbufs_BtoA : attributes->nbufs_AtoB;
  if (nbufs_sender > 0) {
    if (attributes->sender_max_bytes_list == NULL) {
      fprintf(stderr, "ERROR in %s(): attributes->sender_max_bytes_list is NULL\n", __FUNCTION__);
      abort();
    }
    if (attributes->sender_addr_list == NULL) {
      fprintf(stderr, "ERROR in %s(): attributes->sender_addr_list is NULL\n", __FUNCTION__);
      abort();
    }
  } else {
    if (attributes->sender_max_bytes_list != NULL) {
      fprintf(stderr, "ERROR in %s(): Since this endpoint does not have any send buffers, attributes->sender_max_bytes_list must be NULL\n", __FUNCTION__);
      abort();
    }
    if (attributes->sender_addr_list != NULL) {
      fprintf(stderr, "ERROR in %s(): Since this endpoint does not have any send buffers, attributes->sender_addr_list must be NULL\n", __FUNCTION__);
      abort();
    }
  }
  if (nbufs_recver > 0) {
    if (attributes->recver_max_bytes_list == NULL) {
      fprintf(stderr, "ERROR in %s(): attributes->recver_max_bytes_list is NULL\n", __FUNCTION__);
      abort();
    }
    if (attributes->recver_addr_list == NULL) {
      fprintf(stderr, "ERROR in %s(): attributes->recver_addr_list is NULL\n", __FUNCTION__);
      abort();
    }
  } else {
    if (attributes->recver_max_bytes_list != NULL) {
      fprintf(stderr, "ERROR in %s(): Since this endpoint does not have any recv buffers, attributes->recver_max_bytes_list must be NULL\n", __FUNCTION__);
      abort();
    }
    if (attributes->recver_addr_list != NULL) {
      fprintf(stderr, "ERROR in %s(): Since this endpoint does not have any recv buffers, attributes->recver_addr_list must be NULL\n", __FUNCTION__);
      abort();
    }
  }

  // Allocate space for error messages
  // IMPORTANT: If takyonCreate() fails, then the app must free this memory, otherwise Takyon will free this memory
  attributes->error_message = (char *)malloc(MAX_ERROR_MESSAGE_CHARS);
  if (attributes->error_message == NULL) {
    fprintf(stderr, "Out of memory when allocating attrs->error_message\n");
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }
  clearErrorMessage(attributes->is_endpointA, attributes->error_message);

  // Get the interconnect type
  char interconnect_module[TAKYON_MAX_INTERCONNECT_CHARS];
  if (!argGetInterconnect(attributes->interconnect, interconnect_module, TAKYON_MAX_INTERCONNECT_CHARS, attributes->error_message)) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Failed to get module name from interconnect text\n");
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }

  // Load the shared library for the specified interconnect 
  if (!sharedLibraryLoad(interconnect_module, attributes->verbosity & TAKYON_VERBOSITY_CREATE_DESTROY, attributes->error_message)) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Failed to load the Takyon shared library for the interconnect module '%s'.\n", interconnect_module);
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }

  // Create the path structure;
  TakyonPath *path = (TakyonPath *)calloc(1, sizeof(TakyonPath));
  if (path == NULL) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Out of memory\n");
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }

  // Create the private path with the private attributes
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)calloc(1, sizeof(TakyonPrivatePath));
  if (private_path == NULL) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Out of memory\n");
    free(path);
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }
  path->private_path = private_path;

  // Convert the timeout values from double (seconds) to int64_t (nanoseconds)
  private_path->path_create_timeout_ns  = (int64_t)(attributes->path_create_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->send_start_timeout_ns   = (int64_t)(attributes->send_start_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->send_finish_timeout_ns  = (int64_t)(attributes->send_finish_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->recv_start_timeout_ns   = (int64_t)(attributes->recv_start_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->recv_finish_timeout_ns  = (int64_t)(attributes->recv_finish_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->path_destroy_timeout_ns = (int64_t)(attributes->path_destroy_timeout * NANOSECONDS_PER_SECOND_DOUBLE);

  // Get the function pointers from the loaded shared object library
  if (!sharedLibraryGetInterconnectFunctionPointers(interconnect_module, private_path, attributes->error_message)) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Failed to determine the functionality for the '%s' interconnect. If using Takyon's shared libraries, make sure the environment variable TAKYON_PATH is pointing to the correct folder. If using Takyon's static library, make sure the executable is linked with the correct static library. Otherwise the interconnect may not be available on this platform.\n", interconnect_module);
    checkFailureReportingMethod(attributes, __FUNCTION__);
    free(private_path);
    free(path);
    return NULL;
  }

  // Copy over all of the attributes
  path->attrs = *attributes;

  // Mark the functions that are supported for this interconnect
  path->attrs.Send_supported = (private_path->tknSend != NULL);
  path->attrs.IsSent_supported = (private_path->tknIsSent != NULL);
  path->attrs.PostRecv_supported = (private_path->tknPostRecv != NULL);
  path->attrs.Recv_supported = (private_path->tknRecv != NULL);

  // Clear the fields that should not be copied from the original attributes
  path->attrs.sender_max_bytes_list = NULL;
  path->attrs.recver_max_bytes_list = NULL;
  path->attrs.sender_addr_list = NULL;
  path->attrs.recver_addr_list = NULL;

  // Allocate the memory for the buffer lists
  if (nbufs_sender > 0) path->attrs.sender_max_bytes_list = (uint64_t *)calloc(nbufs_sender, sizeof(uint64_t));
  if (nbufs_recver > 0) path->attrs.recver_max_bytes_list = (uint64_t *)calloc(nbufs_recver, sizeof(uint64_t));
  if (nbufs_sender > 0) path->attrs.sender_addr_list = (size_t *)calloc(nbufs_sender, sizeof(size_t));
  if (nbufs_recver > 0) path->attrs.recver_addr_list = (size_t *)calloc(nbufs_recver, sizeof(size_t));
  if (((nbufs_sender > 0) && (path->attrs.sender_max_bytes_list == NULL)) ||
      ((nbufs_recver > 0) && (path->attrs.recver_max_bytes_list == NULL)) ||
      ((nbufs_sender > 0) && (path->attrs.sender_addr_list == NULL)) ||
      ((nbufs_recver > 0) && (path->attrs.recver_addr_list == NULL))) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Out of memory\n");
    if (path->attrs.sender_max_bytes_list != NULL) free(path->attrs.sender_max_bytes_list);
    if (path->attrs.recver_max_bytes_list != NULL) free(path->attrs.recver_max_bytes_list);
    if (path->attrs.sender_addr_list != NULL) free(path->attrs.sender_addr_list);
    if (path->attrs.recver_addr_list != NULL) free(path->attrs.recver_addr_list);
    free(path->private_path);
    free(path);
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }

  // Copy over the info for the buffer lists
  for (int i=0; i<nbufs_sender; i++) {
    path->attrs.sender_max_bytes_list[i] = attributes->sender_max_bytes_list[i];
  }
  for (int i=0; i<nbufs_recver; i++) {
    path->attrs.recver_max_bytes_list[i] = attributes->recver_max_bytes_list[i];
  }
  for (int i=0; i<nbufs_sender; i++) {
    path->attrs.sender_addr_list[i] = attributes->sender_addr_list[i];
  }
  for (int i=0; i<nbufs_recver; i++) {
    path->attrs.recver_addr_list[i] = attributes->recver_addr_list[i];
  }

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY) {
    printf("%-15s (%s:%s) endian=%s, bits=%d, %s\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           endianIsBig() ? "big" : "little",
           (int)sizeof(int*)*8,
           path->attrs.is_polling ? "polling" : "event driven");
  }

  // Initialize the actual connection (this is a blocking coordination between side A and B)
  bool ok = private_path->tknCreate(path);
  if (!ok) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Failed to make connection with %s:%s\n", path->attrs.is_endpointA ? "A" : "B", path->attrs.interconnect);
    free(path->attrs.sender_max_bytes_list);
    free(path->attrs.recver_max_bytes_list);
    free(path->attrs.sender_addr_list);
    free(path->attrs.recver_addr_list);
    free(path->private_path);
    free(path);
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }

  return path;
}

bool takyonSend(TakyonPath *path, int buffer_index, TakyonSendFlagsMask flags, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV) {
    printf("%-15s (%s:%s) buf=%d, addr=%ju, %ju bytes, soff=%ju, doff=%ju\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index,
           path->attrs.sender_addr_list[buffer_index] + src_offset,
           bytes,
           src_offset,
           dest_offset);
  }

  // Error checking
  if (private_path->tknSend == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This Takyon interconnect does not support takyonSend(). Use a different Takyon interconnect.\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  if (nbufs_sender <= 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Can't call takyonSend() on this path since there are no send buffers defined.\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (buffer_index >= nbufs_sender) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index=%d must be less than the number of send buffers=%d\n", buffer_index, nbufs_sender);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (((private_path->send_start_timeout_ns >= 0) || (private_path->send_finish_timeout_ns >= 0)) && (timed_out_ret == NULL)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "send_start_timeout and/or send_finish_timeout was set, so timed_out_ret must not be NULL\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  uint64_t max_sender_bytes = path->attrs.sender_max_bytes_list[buffer_index];
  uint64_t total_bytes = bytes + src_offset;
  if (total_bytes > max_sender_bytes) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Can't send more than %ju bytes but trying to send %ju plus src_offset of %ju\n", max_sender_bytes, bytes, src_offset);
    return false;
  }

  // Init output values
  if (timed_out_ret != NULL) *timed_out_ret = false;

  // Initiate the send
  bool ok = private_path->tknSend(path, buffer_index, flags, bytes, src_offset, dest_offset, timed_out_ret);
  if (!ok) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Check for hidden errors
  if (strlen(path->attrs.error_message) > 0) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  return ok;
}

bool takyonIsSent(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV) {
    printf("%-15s (%s:%s) buf=%d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index);
  }

  // Error checking
  if (private_path->tknIsSent == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This Takyon interconnect does not support takyonIsSent(). Use a different Takyon interconnect or avoid the flag TAKYON_SEND_FLAGS_NON_BLOCKING when sending.\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  if (nbufs_sender <= 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Can't call takyonIsSent() on this path since there are no send buffers defined.\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (buffer_index >= nbufs_sender) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index=%d must be less than the number of send buffers=%d\n", buffer_index, nbufs_sender);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if ((private_path->send_finish_timeout_ns >= 0) && (timed_out_ret == NULL)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "send_finish_timeout was set, so timed_out_ret must not be NULL\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Init output values
  if (timed_out_ret != NULL) *timed_out_ret = false;

  // Check for send completion
  bool ok = private_path->tknIsSent(path, buffer_index, timed_out_ret);
  if (!ok) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Check for hidden errors
  if (strlen(path->attrs.error_message) > 0) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  return ok;
}

bool takyonRecv(TakyonPath *path, int buffer_index, TakyonRecvFlagsMask flags, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV) {
    printf("%-15s (%s:%s) buf=%d. Waiting for data\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index);
  }

  // Error checking
  if (private_path->tknRecv == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This Takyon interconnect does not support takyonRecv(). Use a different Takyon interconnect.\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  if (nbufs_recver <= 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Can't call takyonRecv() on this path since there are no recv buffers defined.\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (buffer_index >= nbufs_recver) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index=%d must be less than the number of recv buffers=%d\n", buffer_index, nbufs_recver);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  // NOTE: bytes_ret and offset_ret are allowed to be NULL, where timed_out_ret is only allowed to be NULL if the associated timeouts are TAKYON_WAIT_FOREVER
  if (((private_path->recv_start_timeout_ns >= 0) || (private_path->recv_finish_timeout_ns >= 0)) && (timed_out_ret == NULL)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "recv_start_timeout and/or recv_finish_timeout was set, so timed_out_ret must not be NULL\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Init output values
  if (timed_out_ret != NULL) *timed_out_ret = false;
  if (bytes_ret != NULL) *bytes_ret = 0;
  if (offset_ret != NULL) *offset_ret = 0;

  uint64_t bytes = 0;
  uint64_t offset = 0;
  // Some interconnects (e.g. OneSidedSocket) need to kown how many bytes to expect and the expected offest
  if (bytes_ret != NULL) bytes = *bytes_ret;
  if (offset_ret != NULL) offset = *offset_ret;
  bool ok = private_path->tknRecv(path, buffer_index, flags, &bytes, &offset, timed_out_ret);
  if (!ok) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Check for hidden errors
  if (strlen(path->attrs.error_message) > 0) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV) {
    printf("%-15s (%s:%s) buf=%d. Got data: %ju bytes at offset %ju\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index,
           bytes,
           offset);
  }

  if (bytes_ret != NULL) *bytes_ret = bytes;
  if (offset_ret != NULL) *offset_ret = offset;

  return ok;
}

bool takyonPostRecv(TakyonPath *path, int buffer_index) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_SEND_RECV) {
    printf("%-15s (%s:%s) buf=%d. Post a new receive\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index);
  }

  // Error checking
  if (private_path->tknPostRecv == NULL) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "This Takyon interconnect does not support takyonPostRecv(). Use a different Takyon interconnect or avoid the flag TAKYON_RECV_FLAGS_MANUAL_REPOST when receiving\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  if (nbufs_recver <= 0) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Can't call takyonPostRecv() on this path since there are no recv buffers defined.\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (buffer_index >= nbufs_recver) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index=%d must be less than the number of recv buffers=%d\n", buffer_index, nbufs_recver);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Post the recv
  bool ok = private_path->tknPostRecv(path, buffer_index);
  if (!ok) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Check for hidden errors
  if (strlen(path->attrs.error_message) > 0) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  return ok;
}

char *takyonDestroy(TakyonPath **path_ret) {
  TakyonPath *path = *path_ret;
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private_path;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_CREATE_DESTROY) {
    printf("%-15s (%s:%s)\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect);
  }

  // Do a coordinated shutdown with side A and B (this is a blocking call)
  bool ok = private_path->tknDestroy(path);
  if (!ok) {
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
  } else {
    // Get the interconnect type
    char interconnect_module[TAKYON_MAX_INTERCONNECT_CHARS];
    ok = argGetInterconnect(path->attrs.interconnect, interconnect_module, TAKYON_MAX_INTERCONNECT_CHARS, path->attrs.error_message);
    if (!ok) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to get module name from interconnect text\n");
      checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    } else {
      // Load the shared library for the specified interconnect 
      ok = sharedLibraryUnload(interconnect_module, path->attrs.error_message);
      if (!ok) {
        TAKYON_RECORD_ERROR(path->attrs.error_message, "Failed to unload the Takyon shared library for the interconnect module '%s'.\n", interconnect_module);
        checkFailureReportingMethod(&path->attrs, __FUNCTION__);
      }
    }
  }

  // Check if there was an error
  char *error_message = path->attrs.error_message;
  if (ok) {
    free(error_message);
    error_message = NULL;
  }

  // Free resources
  free(path->attrs.sender_max_bytes_list);
  free(path->attrs.recver_max_bytes_list);
  free(path->attrs.sender_addr_list);
  free(path->attrs.recver_addr_list);
  free(path->private_path);
  free(path);

  *path_ret = NULL;

  return error_message;
}
