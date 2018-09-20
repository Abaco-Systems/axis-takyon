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

#include "takyon_extensions.h"

TakyonCollectiveBarrier *takyonBarrierInit(int nchildren, TakyonPath *parent_path, TakyonPath **child_path_list) {
  if (nchildren < 0) {
    fprintf(stderr, "%s(): npaths must be >= zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if ((nchildren > 0) && (child_path_list == NULL)) {
    fprintf(stderr, "%s(): child_path_list is NULL but nchildren is %d\n", __FUNCTION__, nchildren);
    exit(EXIT_FAILURE);
  }
  if ((nchildren == 0) && (child_path_list != NULL)) {
    fprintf(stderr, "%s(): child_path_list is not NULL but nchildren is %d\n", __FUNCTION__, nchildren);
    exit(EXIT_FAILURE);
  }
  TakyonCollectiveBarrier *collective = (TakyonCollectiveBarrier *)calloc(1, sizeof(TakyonCollectiveBarrier));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->parent_path = parent_path;
  collective->nchildren = nchildren;
  if (nchildren > 0) {
    collective->child_path_list = (TakyonPath **)calloc(nchildren, sizeof(TakyonPath *));
    if (collective->child_path_list == NULL) {
      fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    for (int i=0; i<nchildren; i++) {
      if (child_path_list[i] == NULL) {
        fprintf(stderr, "%s(): child_path_list[%d] is NULL\n", __FUNCTION__, i);
        exit(EXIT_FAILURE);
      }
      collective->child_path_list[i] = child_path_list[i];
    }
  }
  return collective;
}

void takyonBarrierRun(TakyonCollectiveBarrier *collective, int buffer) {
  bool timed_out;
  bool ok;

  // NOTE: This is a tree based barrier.
  // Signaling starts at the top of the tree and works it way down to the leaves.
  // Once all the leaves get the signal, then it works its way back to the top of the tree.
  // This is a log(N) barrier, where the base of the log() is the number of children per node.

  // If parent exists, wait for a signal
  if (collective->parent_path != NULL) {
    ok = takyonRecv(collective->parent_path, buffer, NULL, NULL, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->parent_path->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }

  // If have children, send a signal, then wait for a response
  for (int i=0; i<collective->nchildren; i++) {
    // Send signal
    ok = takyonSend(collective->child_path_list[i], buffer, 0, 0, 0, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): path index %d timed out.\n", __FUNCTION__, i);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): path index %d failed with error:\n%s\n", __FUNCTION__, i, collective->child_path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
  for (int i=0; i<collective->nchildren; i++) {
    // Wait for response
    ok = takyonRecv(collective->child_path_list[i], buffer, NULL, NULL, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->child_path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }

  // If parent exists, send a response
  if (collective->parent_path != NULL) {
    // Send signal
    ok = takyonSend(collective->parent_path, buffer, 0, 0, 0, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->parent_path->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
}

void takyonBarrierFinalize(TakyonCollectiveBarrier *collective) {
  free(collective->child_path_list);
  free(collective);
}

TakyonCollectiveReduce *takyonReduceInit(int nchildren, TakyonPath *parent_path, TakyonPath **child_path_list) {
  if (nchildren < 0) {
    fprintf(stderr, "%s(): npaths must be >= zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if ((nchildren > 0) && (child_path_list == NULL)) {
    fprintf(stderr, "%s(): child_path_list is NULL but nchildren is %d\n", __FUNCTION__, nchildren);
    exit(EXIT_FAILURE);
  }
  if ((nchildren == 0) && (child_path_list != NULL)) {
    fprintf(stderr, "%s(): child_path_list is not NULL but nchildren is %d\n", __FUNCTION__, nchildren);
    exit(EXIT_FAILURE);
  }
  TakyonCollectiveReduce *collective = (TakyonCollectiveReduce *)calloc(1, sizeof(TakyonCollectiveReduce));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->parent_path = parent_path;
  collective->nchildren = nchildren;
  if (nchildren > 0) {
    collective->child_path_list = (TakyonPath **)calloc(nchildren, sizeof(TakyonPath *));
    if (collective->child_path_list == NULL) {
      fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    for (int i=0; i<nchildren; i++) {
      if (child_path_list[i] == NULL) {
        fprintf(stderr, "%s(): child_path_list[%d] is NULL\n", __FUNCTION__, i);
        exit(EXIT_FAILURE);
      }
      collective->child_path_list[i] = child_path_list[i];
    }
  }
  return collective;
}

static void takyonReduceRun(TakyonCollectiveReduce *collective, int buffer, uint64_t nelements, uint64_t bytes_per_elem, void(*reduce_function)(uint64_t nelements,void *a,void *b), void *a, bool scatter_result) {
  bool timed_out;
  bool ok;
  uint64_t bytes_expected = nelements * bytes_per_elem;

  // NOTE: This is a tree based reduction.
  // The reduction starts at the leaves and works it way up to the top of the tree.
  // This is a log(N) reduction, where the base of the log() is the number of children per node.

  // If have children, then wait for thier data to arrive
  for (int i=0; i<collective->nchildren; i++) {
    // Wait for the data
    uint64_t bytes_received;
    ok = takyonRecv(collective->child_path_list[i], buffer, &bytes_received, NULL, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->child_path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
    if (bytes_received != bytes_expected) {
      fprintf(stderr, "%s(): Received %llu bytes but expected %llu\n", __FUNCTION__, (unsigned long long)bytes_received, (unsigned long long)bytes_expected);
      exit(EXIT_FAILURE);
    }
    // Reduce the data
    // NOTE: The 'a' vector comes from the send buffer of the parent pathi fi it exists, otherwise it's the root vector
    void *b = (void *)collective->child_path_list[i]->attrs.recver_addr_list[buffer];
    // This is an inplace operation where the result goes into vector 'a'
    reduce_function(nelements, a, b);
  }

  // If parent exists, send the reduced data
  if (collective->parent_path != NULL) {
    // Send reduced value which is already in the sender's buffer
    ok = takyonSend(collective->parent_path, buffer, bytes_expected, 0, 0, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->parent_path->attrs.error_message);
      exit(EXIT_FAILURE);
    }

  } else {
    // This is the root of the tree
    // The results are in the 'a' vector
  }

  // The root thrad has the result now, so send it to all the children
  if (scatter_result) {
    // Pass the results from the root to all children
    if (collective->parent_path != NULL) {
      // Wait for data from the parent
      ok = takyonRecv(collective->parent_path, buffer, NULL, NULL, &timed_out);
      if (ok && timed_out) {
        fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
        exit(EXIT_FAILURE);
      }
      if (!ok) {
        fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->parent_path->attrs.error_message);
        exit(EXIT_FAILURE);
      }
    }
    // Send results to the children
    for (int i=0; i<collective->nchildren; i++) {
      // Move the data to the send buffer
      if (collective->parent_path == NULL) {
        // This is the root
        memcpy((void *)collective->child_path_list[i]->attrs.sender_addr_list[buffer], a, bytes_expected);
      } else {
        // This is a child
        memcpy((void *)collective->child_path_list[i]->attrs.sender_addr_list[buffer], (void *)collective->parent_path->attrs.recver_addr_list[buffer], bytes_expected);
      }
      // Send the data to the child
      ok = takyonSend(collective->child_path_list[i], buffer, bytes_expected, 0, 0, &timed_out);
      if (ok && timed_out) {
        fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
        exit(EXIT_FAILURE);
      }
      if (!ok) {
        fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->child_path_list[i]->attrs.error_message);
        exit(EXIT_FAILURE);
      }
    }
  }
}

void takyonReduceRoot(TakyonCollectiveReduce *collective, int buffer, uint64_t nelements, uint64_t bytes_per_elem, void(*reduce_function)(uint64_t nelements,void *a,void *b), void *data, bool scatter_result) {
  if (collective->parent_path != NULL) {
    fprintf(stderr, "%s(): This thread is not the root of this reduce collective; use takyonReduceChild() instead.\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  takyonReduceRun(collective, buffer, nelements, bytes_per_elem, reduce_function, data, scatter_result);
}

void takyonReduceChild(TakyonCollectiveReduce *collective, int buffer, uint64_t nelements, uint64_t bytes_per_elem, void(*reduce_function)(uint64_t nelements,void *a,void *b), bool scatter_result) {
  if (collective->parent_path == NULL) {
    fprintf(stderr, "%s(): This thread is the root of this reduce collective; use takyonReduceRoot() instead.\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  // NOTE: This thread's data is in the senders buffer
  void *data = (void *)collective->parent_path->attrs.sender_addr_list[buffer];
  takyonReduceRun(collective, buffer, nelements, bytes_per_elem, reduce_function, data, scatter_result);
}

void takyonReduceFinalize(TakyonCollectiveReduce *collective) {
  free(collective->child_path_list);
  free(collective);
}

TakyonCollectiveOne2One *takyonOne2OneInit(int npaths, int num_src_paths, int num_dest_paths, TakyonPath **src_path_list, TakyonPath **dest_path_list) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if ((num_src_paths > 0) && (src_path_list == NULL)) {
    fprintf(stderr, "%s(): src_path_list is NULL, but num_src_paths=%d\n", __FUNCTION__, num_src_paths);
    exit(EXIT_FAILURE);
  }
  if ((num_dest_paths > 0) && (dest_path_list == NULL)) {
    fprintf(stderr, "%s(): dest_path_list is NULL, but num_dest_paths=%d\n", __FUNCTION__, num_dest_paths);
    exit(EXIT_FAILURE);
  }
  TakyonCollectiveOne2One *collective = (TakyonCollectiveOne2One *)calloc(1, sizeof(TakyonCollectiveOne2One));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->src_path_list = (TakyonPath **)calloc(num_src_paths, sizeof(TakyonPath *));
  if (collective->src_path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->dest_path_list = (TakyonPath **)calloc(num_dest_paths, sizeof(TakyonPath *));
  if (collective->dest_path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  for (int i=0; i<num_src_paths; i++) {
    collective->src_path_list[i] = src_path_list[i];
  }
  for (int i=0; i<num_dest_paths; i++) {
    collective->dest_path_list[i] = dest_path_list[i];
  }
  return collective;
}

void takyonOne2OneFinalize(TakyonCollectiveOne2One *collective) {
  free(collective->src_path_list);
  free(collective->dest_path_list);
  free(collective);
}

TakyonScatterSrc *takyonScatterSrcInit(int npaths, TakyonPath **path_list) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path_list == NULL) {
    fprintf(stderr, "%s(): path_list is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  for (int i=0; i<npaths; i++) {
    if (path_list[i] == NULL) {
      fprintf(stderr, "%s(): path_list[%d] is NULL\n", __FUNCTION__, i);
      exit(EXIT_FAILURE);
    }
  }
  TakyonScatterSrc *collective = (TakyonScatterSrc *)calloc(1, sizeof(TakyonScatterSrc));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->path_list = (TakyonPath **)calloc(npaths, sizeof(TakyonPath *));
  if (collective->path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  for (int i=0; i<npaths; i++) {
    collective->path_list[i] = path_list[i];
  }
  return collective;
}

TakyonScatterDest *takyonScatterDestInit(int npaths, int path_index, TakyonPath *path) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path_index < 0 || path_index >= npaths) {
    fprintf(stderr, "%s(): path_index must be >= 0 and < npaths\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path == NULL) {
    fprintf(stderr, "%s(): path is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  TakyonScatterDest *collective = (TakyonScatterDest *)calloc(1, sizeof(TakyonScatterDest));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  collective->path_index = path_index;
  collective->path = path;
  return collective;
}

void takyonScatterSend(TakyonScatterSrc *collective, int buffer, uint64_t *nbytes_list, uint64_t *soffset_list, uint64_t *doffset_list) {
  for (int i=0; i<collective->npaths; i++) {
    bool timed_out;
    bool ok = takyonSend(collective->path_list[i], buffer, nbytes_list[i], soffset_list[i], doffset_list[i], &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): path index %d timed out.\n", __FUNCTION__, i);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): path index %d failed with error:\n%s\n", __FUNCTION__, i, collective->path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
  // If non blocking, then wait for completion
  for (int i=0; i<collective->npaths; i++) {
    if (collective->path_list[i]->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
      bool timed_out;
      bool ok = takyonSendTest(collective->path_list[i], buffer, &timed_out);
      if (ok && timed_out) {
        fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
        exit(EXIT_FAILURE);
      }
      if (!ok) {
        fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path_list[i]->attrs.error_message);
        exit(EXIT_FAILURE);
      }
    }
  }
}

void takyonScatterRecv(TakyonScatterDest *collective, int buffer, uint64_t *nbytes_ret, uint64_t *offset_ret) {
  bool timed_out;
  bool ok = takyonRecv(collective->path, buffer, nbytes_ret, offset_ret, &timed_out);
  if (ok && timed_out) {
    fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (!ok) {
    fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path->attrs.error_message);
    exit(EXIT_FAILURE);
  }
}

void takyonScatterSrcFinalize(TakyonScatterSrc *collective) {
  free(collective->path_list);
  free(collective);
}

void takyonScatterDestFinalize(TakyonScatterDest *collective) {
  free(collective);
}

TakyonGatherSrc *takyonGatherSrcInit(int npaths, int path_index, TakyonPath *path) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path_index < 0 || path_index >= npaths) {
    fprintf(stderr, "%s(): path_index must be >= 0 and < npaths\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path == NULL) {
    fprintf(stderr, "%s(): path is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  TakyonGatherSrc *collective = (TakyonGatherSrc *)calloc(1, sizeof(TakyonGatherSrc));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  collective->path_index = path_index;
  collective->path = path;
  return collective;
}

TakyonGatherDest *takyonGatherDestInit(int npaths, TakyonPath **path_list) {
  if (npaths <= 0) {
    fprintf(stderr, "%s(): npaths must be greater than zero\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (path_list == NULL) {
    fprintf(stderr, "%s(): path_list is NULL\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  for (int i=0; i<npaths; i++) {
    if (path_list[i] == NULL) {
      fprintf(stderr, "%s(): path_list[%d] is NULL\n", __FUNCTION__, i);
      exit(EXIT_FAILURE);
    }
  }
  TakyonGatherDest *collective = (TakyonGatherDest *)calloc(1, sizeof(TakyonGatherDest));
  if (collective == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->path_list = (TakyonPath **)calloc(npaths, sizeof(TakyonPath *));
  if (collective->path_list == NULL) {
    fprintf(stderr, "%s(): Out of memory\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  collective->npaths = npaths;
  for (int i=0; i<npaths; i++) {
    collective->path_list[i] = path_list[i];
  }
  return collective;
}

void takyonGatherSend(TakyonGatherSrc *collective, int buffer, uint64_t nbytes, uint64_t soffset, uint64_t doffset) {
  bool timed_out;
  bool ok = takyonSend(collective->path, buffer, nbytes, soffset, doffset, &timed_out);
  if (ok && timed_out) {
    fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
    exit(EXIT_FAILURE);
  }
  if (!ok) {
    fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path->attrs.error_message);
    exit(EXIT_FAILURE);
  }
  // If non blocking, then wait for completion
  if (collective->path->attrs.send_completion_method == TAKYON_USE_SEND_TEST) {
    ok = takyonSendTest(collective->path, buffer, &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
}

void takyonGatherRecv(TakyonGatherDest *collective, int buffer, uint64_t *nbytes_list_ret, uint64_t *offset_list_ret) {
  for (int i=0; i<collective->npaths; i++) {
    bool timed_out;
    bool ok = takyonRecv(collective->path_list[i], buffer, &nbytes_list_ret[i], &offset_list_ret[i], &timed_out);
    if (ok && timed_out) {
      fprintf(stderr, "%s(): timed out.\n", __FUNCTION__);
      exit(EXIT_FAILURE);
    }
    if (!ok) {
      fprintf(stderr, "%s(): failed with error:\n%s\n", __FUNCTION__, collective->path_list[i]->attrs.error_message);
      exit(EXIT_FAILURE);
    }
  }
}

void takyonGatherSrcFinalize(TakyonGatherSrc *collective) {
  free(collective);
}

void takyonGatherDestFinalize(TakyonGatherDest *collective) {
  free(collective->path_list);
  free(collective);
}
