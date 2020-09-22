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
//   Some helpful endian functions just in case the implementation needs it.
// -----------------------------------------------------------------------------

#include "takyon_private.h"

bool endianIsBig() {
  long int testInt = 0x12345678;
  char *ptr = (char *)&testInt;
  if (ptr[0] == 0x78) {
    return false;
  } else {
    return true;
  }
}

void endianSwapUInt16(uint16_t *data, uint64_t num_elements) {
  for (int i=0; i<num_elements; i++) {
    uint16_t value = data[i];
    data[i] = (value>>8) | (value<<8);
  }
}

void endianSwapUInt32(uint32_t *data, uint64_t num_elements) {
  for (int i=0; i<num_elements; i++) {
    uint32_t value = data[i];
    value = ((value << 8) & 0xFF00FF00) | ((value >> 8) & 0xFF00FF);
    data[i] = (value << 16) | (value >> 16);
  }
}
