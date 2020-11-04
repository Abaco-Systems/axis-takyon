// Copyright 2020 Abaco Systems
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
//   Some helpful CUDA memory allocation functions.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

bool isCudaAddress(void *addr, bool *is_cuda_addr_ret, char *error_message) {
  struct cudaPointerAttributes attributes;
  cudaError_t cuda_status = cudaPointerGetAttributes(&attributes, addr);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to determine if memory addr is CUDA or CPU, operation may not be supported on this platform: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  
  *is_cuda_addr_ret = (attributes.type == cudaMemoryTypeDevice);
  return true;
}

void *cudaMemoryAlloc(int cuda_device_id, uint64_t bytes, char *error_message) {
  if (cuda_device_id < 0) {
    TAKYON_RECORD_ERROR(error_message, "The device ID %d is not a valid CUDA device\n", cuda_device_id);
    return NULL;
  }

  // Get the current device ID so it can be restored after the allocation
  int current_cuda_device_id;
  cudaError_t cuda_status = cudaGetDevice(&current_cuda_device_id);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Unable to get the current cuda device ID: %s\n", cudaGetErrorString(cuda_status));
    return NULL;
  }

  // Set the required device ID for the allocation
  if (current_cuda_device_id != cuda_device_id) {
    cuda_status = cudaSetDevice(cuda_device_id);
    if (cuda_status != cudaSuccess) {
      TAKYON_RECORD_ERROR(error_message, "Failed to set the cuda device ID to %d: %s\n", cuda_device_id, cudaGetErrorString(cuda_status));
      return NULL;
    }
  }

  // Allocate the CUDA memory
  void *cuda_address = NULL;
  cuda_status = cudaMalloc(&cuda_address, bytes);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to allocate CUDA memory buffer (%ju bytes): %s\n", bytes, cudaGetErrorString(cuda_status));
    return NULL;
  }

  // Revert to previous CUDA device
  if (current_cuda_device_id != cuda_device_id) {
    cuda_status = cudaSetDevice(current_cuda_device_id);
    if (cuda_status != cudaSuccess) {
      cudaFree(cuda_address);
      TAKYON_RECORD_ERROR(error_message, "Failed to revert to the previous cuda device ID %d: %s\n", current_cuda_device_id, cudaGetErrorString(cuda_status));
      return NULL;
    }
  }

  return cuda_address;
}

bool cudaMemoryFree(void *addr, char *error_message) {
  cudaError_t cuda_status = cudaFree(addr);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to free CUDA buffer: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  return true;
}

// The creator of the memory needs to call this before passing (via some other IPC) it to the remote endpoint
bool cudaCreateIpcMapFromLocalAddr(void *cuda_addr, cudaIpcMemHandle_t *ipc_map_ret, char *error_message) {
  cudaError_t cuda_status = cudaIpcGetMemHandle(ipc_map_ret, cuda_addr);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to create a map to the CUDA address: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  return true;
  // NOTE: Does not appear to be a function to un'Get', so no need for a complimenting destroy function
}

// The remote endpoint calls this
void *cudaGetRemoteAddrFromIpcMap(cudaIpcMemHandle_t remote_ipc_map, char *error_message) {
  void *remote_cuda_addr = NULL;
  unsigned int flags = cudaIpcMemLazyEnablePeerAccess; // IMPORTANT: cudaIpcMemLazyEnablePeerAccess is required and looks to be the only option
  cudaError_t cuda_status = cudaIpcOpenMemHandle(&remote_cuda_addr, remote_ipc_map, flags);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to get a remote CUDA address from an IPC mapping: %s\n", cudaGetErrorString(cuda_status));
    return NULL;
  }
  return remote_cuda_addr;
}

// The remote endpoint calls this
bool cudaReleaseRemoteIpcMappedAddr(void *mapped_cuda_addr, char *error_message) {
  cudaError_t cuda_status = cudaIpcCloseMemHandle(mapped_cuda_addr);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to release the remote CUDA mapped address: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  return true;
}

bool cudaEventAlloc(int cuda_device_id, cudaEvent_t *event_ret, char *error_message) {
  if (cuda_device_id < 0) {
    TAKYON_RECORD_ERROR(error_message, "The device ID %d is not a valid CUDA device\n", cuda_device_id);
    return false;
  }

  // Get the current device ID so it can be restored after the allocation
  int current_cuda_device_id;
  cudaError_t cuda_status = cudaGetDevice(&current_cuda_device_id);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Unable to get the current cuda device ID: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }

  // Set the required device ID for the allocation
  if (current_cuda_device_id != cuda_device_id) {
    cuda_status = cudaSetDevice(cuda_device_id);
    if (cuda_status != cudaSuccess) {
      TAKYON_RECORD_ERROR(error_message, "Failed to set the cuda device ID to %d: %s\n", cuda_device_id, cudaGetErrorString(cuda_status));
      return false;
    }
  }

  // Allocate the CUDA event
  cuda_status = cudaEventCreateWithFlags(event_ret, cudaEventDisableTiming | cudaEventInterprocess);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to allocate CUDA event: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }

  // Revert to previous CUDA device
  if (current_cuda_device_id != cuda_device_id) {
    cuda_status = cudaSetDevice(current_cuda_device_id);
    if (cuda_status != cudaSuccess) {
      cudaEventDestroy(*event_ret);
      TAKYON_RECORD_ERROR(error_message, "Failed to revert to the previous cuda device ID %d: %s\n", current_cuda_device_id, cudaGetErrorString(cuda_status));
      return false;
    }
  }

  return true;
}

bool cudaEventFree(cudaEvent_t *event, char *error_message) {
  cudaError_t cuda_status = cudaEventDestroy(*event);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to free CUDA event: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  return true;
}

// The creator of the event needs to call this before passing (via some other IPC) it to the remote endpoint
bool cudaCreateIpcMapFromLocalEvent(cudaEvent_t *event, cudaIpcEventHandle_t *ipc_map_ret, char *error_message) {
  cudaError_t cuda_status = cudaIpcGetEventHandle(ipc_map_ret, *event);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to create a map to the CUDA event: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  return true;
  // NOTE: Does not appear to be a function to un'Get', so no need for a complimenting destroy function
}

// The remote endpoint calls this
bool cudaGetRemoteEventFromIpcMap(cudaIpcEventHandle_t remote_ipc_map, cudaEvent_t *remote_event_ret, char *error_message) {
  cudaError_t cuda_status = cudaIpcOpenEventHandle(remote_event_ret, remote_ipc_map);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to get a remote CUDA event from an IPC mapping: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  return true;
}

// Either endpoint can call this, but really meant for the sender to call after the cuda memcpy is complete
bool cudaEventNotify(cudaEvent_t *event, char *error_message) {
  cudaError_t cuda_status = cudaEventRecord(*event, 0);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to mark CUDA event as notified: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  return true;
}

// Either endpoint can call this, but really meant for the recever to call to block waiting for the sender to complete its transfer
bool cudaEventWait(cudaEvent_t *event, char *error_message) {
  cudaError_t cuda_status = cudaEventSynchronize(*event);
  if (cuda_status != cudaSuccess) {
    TAKYON_RECORD_ERROR(error_message, "Failed to wait for notification from CUDA event: %s\n", cudaGetErrorString(cuda_status));
    return false;
  }
  return true;
}
