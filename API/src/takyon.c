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
//   This is the core of Takyon; i.e. the glue between the application and the
//   interconnects that Takyon supports
// -----------------------------------------------------------------------------

#include "takyon_private.h"

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
  if (attributes->send_completion_method != TAKYON_BLOCKING && attributes->send_completion_method != TAKYON_USE_SEND_TEST) {
    fprintf(stderr, "ERROR in %s(): attributes->send_completion_method must be one of TAKYON_BLOCKING or TAKYON_USE_SEND_TEST\n", __FUNCTION__);
    abort();
  }
  if (attributes->recv_completion_method != TAKYON_BLOCKING) {
    fprintf(stderr, "ERROR in %s(): attributes->recv_completion_method must be TAKYON_BLOCKING\n", __FUNCTION__);
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

  attributes->error_message = (char *)malloc(MAX_ERROR_MESSAGE_CHARS);
  if (attributes->error_message == NULL) {
    fprintf(stderr, "Out of memory when allocating attrs->error_message\n");
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }
  clearErrorMessage(attributes->is_endpointA, attributes->error_message);

  // Get the interconnect type
  char interconnect_module[MAX_TAKYON_INTERCONNECT_CHARS];
  if (!argGet(attributes->interconnect, 0, interconnect_module, MAX_TAKYON_INTERCONNECT_CHARS, attributes->error_message)) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Failed to get module name from interconnect text\n");
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }

  // Load the shared library for the specified interconnect 
  if (!sharedLibraryLoad(interconnect_module, attributes->verbosity & TAKYON_VERBOSITY_INIT, attributes->error_message)) {
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
  path->private = private_path;

  // Convert the timeout values from double (seconds) to int64_t (nanoseconds)
  private_path->create_timeout_ns        = (int64_t)(attributes->create_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->send_start_timeout_ns    = (int64_t)(attributes->send_start_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->send_complete_timeout_ns = (int64_t)(attributes->send_complete_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->recv_complete_timeout_ns = (int64_t)(attributes->recv_complete_timeout * NANOSECONDS_PER_SECOND_DOUBLE);
  private_path->destroy_timeout_ns       = (int64_t)(attributes->destroy_timeout * NANOSECONDS_PER_SECOND_DOUBLE);

  // Get the function pointers from the loaded shared object library
  if (!sharedLibraryGetInterconnectFunctionPointers(interconnect_module, private_path, attributes->error_message)) {
    TAKYON_RECORD_ERROR(attributes->error_message, "Failed to get the '%s' interconnect's function pointers.\n", interconnect_module);
    checkFailureReportingMethod(attributes, __FUNCTION__);
    free(private_path);
    free(path);
    return NULL;
  }

  // Copy over all of the attributes
  path->attrs = *attributes;

  // Clear the fields that should not be copied from the original attributes
  path->attrs.sender_max_bytes_list = NULL;
  path->attrs.recver_max_bytes_list = NULL;
  path->attrs.sender_addr_list = NULL;
  path->attrs.recver_addr_list = NULL;

  // Allocate the memory for the buffer lists
  if (nbufs_sender > 0) path->attrs.sender_max_bytes_list = (uint64_t *)calloc(path->attrs.nbufs_AtoB, sizeof(uint64_t));
  if (nbufs_recver > 0) path->attrs.recver_max_bytes_list = (uint64_t *)calloc(path->attrs.nbufs_BtoA, sizeof(uint64_t));
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
    free(path->private);
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
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT) {
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
    free(path->private);
    free(path);
    checkFailureReportingMethod(attributes, __FUNCTION__);
    return NULL;
  }

  return path;
}

bool takyonSend(TakyonPath *path, int buffer_index, uint64_t bytes, uint64_t src_offset, uint64_t dest_offset, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME) {
    printf("%-15s (%s:%s) buf=%d, %lld bytes, soff=%lld, doff=%lld\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index,
           (unsigned long long)bytes,
           (unsigned long long)src_offset,
           (unsigned long long)dest_offset);
  }

  // Error checking
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  if (buffer_index >= nbufs_sender) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index must be less than %d\n", nbufs_sender);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (((private_path->send_start_timeout_ns >= 0) || (private_path->send_complete_timeout_ns >= 0)) && (timed_out_ret == NULL)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "send_start_timeout and/or send_complete_timeout was set, so timed_out_ret must not be NULL\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  uint64_t max_sender_bytes = path->attrs.sender_max_bytes_list[buffer_index];
  uint64_t total_bytes = bytes + src_offset;
  if (total_bytes > max_sender_bytes) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Can't send more than %lld bytes but trying to send %lld plus src_offset of %lld\n", (unsigned long long)max_sender_bytes, (unsigned long long)bytes, (unsigned long long)src_offset);
    return false;
  }

  // Init output values
  if (timed_out_ret != NULL) *timed_out_ret = false;

  // Initiate the send
  bool ok = private_path->tknSend(path, buffer_index, bytes, src_offset, dest_offset, timed_out_ret);
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

bool takyonSendStrided(TakyonPath *path, int buffer_index, uint64_t num_blocks, uint64_t bytes_per_block, uint64_t src_offset, uint64_t src_stride, uint64_t dest_offset, uint64_t dest_stride, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME) {
    printf("%-15s (%s:%s) buf=%d, num_blocks=%lld, %lld bytes_per_block, soff=%lld, doff=%lld, sstride=%lld, dstride=%lld\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index,
           (unsigned long long)num_blocks,
           (unsigned long long)bytes_per_block,
           (unsigned long long)src_offset,
           (unsigned long long)dest_offset,
           (unsigned long long)src_stride,
           (unsigned long long)dest_stride);
  }

  // Error checking
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  if (buffer_index >= nbufs_sender) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index must be less than %d\n", nbufs_sender);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (((private_path->send_start_timeout_ns >= 0) || (private_path->send_complete_timeout_ns >= 0)) && (timed_out_ret == NULL)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "send_start_timeout and/or send_complete_timeout was set, so timed_out_ret must not be NULL\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (num_blocks>1) {
    if (src_stride < bytes_per_block) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Src stride = %lld, must be greater or equal to bytes per block = %lld\n", (unsigned long long)src_stride, (unsigned long long)bytes_per_block);
      return false;
    }
    if (dest_stride < bytes_per_block) {
      TAKYON_RECORD_ERROR(path->attrs.error_message, "Dest stride = %lld, must be greater or equal to bytes per block = %lld\n", (unsigned long long)dest_stride, (unsigned long long)bytes_per_block);
      return false;
    }
  }
  uint64_t max_sender_bytes = path->attrs.sender_max_bytes_list[buffer_index];
  uint64_t total_bytes = src_offset + num_blocks*bytes_per_block + (num_blocks-1)*src_stride;
  if (total_bytes > max_sender_bytes) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "Exceeding send memory transport bounds: Bounds=%lld bytes but exceeding by %lld bytes\n", (unsigned long long)max_sender_bytes, (unsigned long long)(total_bytes-max_sender_bytes));
    return false;
  }

  // Init output values
  if (timed_out_ret != NULL) *timed_out_ret = false;

  // Initiate the send
  bool ok = private_path->tknSendStrided(path, buffer_index, num_blocks, bytes_per_block, src_offset, src_stride, dest_offset, dest_stride, timed_out_ret);
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

bool takyonSendTest(TakyonPath *path, int buffer_index, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME) {
    printf("%-15s (%s:%s) buf=%d\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index);
  }

  // Error checking
  int nbufs_sender = path->attrs.is_endpointA ? path->attrs.nbufs_AtoB : path->attrs.nbufs_BtoA;
  if (buffer_index >= nbufs_sender) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index must be less than %d\n", nbufs_sender);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if (path->attrs.send_completion_method != TAKYON_USE_SEND_TEST) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "takyon_send_test() cannot be used since the path's send_completion_method is not set to TAKYON_USE_SEND_TEST\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  if ((private_path->send_complete_timeout_ns >= 0) && (timed_out_ret == NULL)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "send_complete_timeout was set, so timed_out_ret must not be NULL\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Init output values
  if (timed_out_ret != NULL) *timed_out_ret = false;

  // Check for send completion
  bool ok = private_path->tknSendTest(path, buffer_index, timed_out_ret);
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

bool takyonRecv(TakyonPath *path, int buffer_index, uint64_t *bytes_ret, uint64_t *offset_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME) {
    printf("%-15s (%s:%s) buf=%d. Waiting for data\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index);
  }

  // Error checking
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  if (buffer_index >= nbufs_recver) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index must be less than %d\n", nbufs_recver);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  // NOTE: num_blocks_ret, bytes_per_block_ret, offset_ret, stride_ret are allowed to be NULL
  if ((private_path->recv_complete_timeout_ns >= 0) && (timed_out_ret == NULL)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A timeout was set, so timed_out_ret must not be NULL\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Init output values
  if (timed_out_ret != NULL) *timed_out_ret = false;

  bool ok = private_path->tknRecv(path, buffer_index, bytes_ret, offset_ret, timed_out_ret);
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

bool takyonRecvStrided(TakyonPath *path, int buffer_index, uint64_t *num_blocks_ret, uint64_t *bytes_per_block_ret, uint64_t *offset_ret, uint64_t *stride_ret, bool *timed_out_ret) {
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_RUNTIME) {
    printf("%-15s (%s:%s) buf=%d. Waiting for data\n",
           __FUNCTION__,
           path->attrs.is_endpointA ? "A" : "B",
           path->attrs.interconnect,
           buffer_index);
  }

  // Error checking
  int nbufs_recver = path->attrs.is_endpointA ? path->attrs.nbufs_BtoA : path->attrs.nbufs_AtoB;
  if (buffer_index >= nbufs_recver) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "The argument buffer_index must be less than %d\n", nbufs_recver);
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }
  // NOTE: num_blocks_ret, bytes_per_block_ret, offset_ret, stride_ret are allowed to be NULL
  if ((private_path->recv_complete_timeout_ns >= 0) && (timed_out_ret == NULL)) {
    TAKYON_RECORD_ERROR(path->attrs.error_message, "A timeout was set, so timed_out_ret must not be NULL\n");
    checkFailureReportingMethod(&path->attrs, __FUNCTION__);
    return false;
  }

  // Init output values
  if (timed_out_ret != NULL) *timed_out_ret = false;

  bool ok = private_path->tknRecvStrided(path, buffer_index, num_blocks_ret, bytes_per_block_ret, offset_ret, stride_ret, timed_out_ret);
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
  TakyonPrivatePath *private_path = (TakyonPrivatePath *)path->private;

  clearErrorMessage(path->attrs.is_endpointA, path->attrs.error_message);

  // Verbosity
  if (path->attrs.verbosity & TAKYON_VERBOSITY_INIT) {
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
    char interconnect_module[MAX_TAKYON_INTERCONNECT_CHARS];
    ok = argGet(path->attrs.interconnect, 0, interconnect_module, MAX_TAKYON_INTERCONNECT_CHARS, path->attrs.error_message);
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
  free(path->private);
  free(path);

  *path_ret = NULL;

  return error_message;
}
