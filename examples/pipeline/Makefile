# Copyright 2018 Abaco Systems
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


APP_NAME       := pipeline
COMMON_FLAGS   := -std=gnu99 -Wall -Werror -pthread
#COMMON_FLAGS   += -g -O0
COMMON_FLAGS   += -O2
CFLAGS         := $(COMMON_FLAGS) -I../../API/inc -I../../extensions
LDFLAGS        := $(COMMON_FLAGS)
C_OBJS         := main.o pipeline.o takyon_graph.o takyon_attributes.o takyon_collective.o takyon_mmap.o
TARGET         := $(APP_NAME)

ifeq ($(TAKYON_LIBS),)
    $(error The environment variable TAKYON_LIBS is undefined)
endif

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    PRJ_LIBS := -rdynamic -Wl,--whole-archive -L$(TAKYON_LIBS) -lTakyon -Wl,--no-whole-archive -ldl -lrt -lm
    ifeq ($(USE_STATIC_LIB),Yes)
        PRJ_LIBS := -L$(TAKYON_LIBS) -lTakyonStatic -lrt -lm
    endif
    ifeq ($(WITH_RDMA),Yes)
        PRJ_LIBS += -libverbs -lrdmacm -lm
    endif
endif
ifeq ($(UNAME_S),Darwin)
    PRJ_LIBS := -rdynamic -all_load -L$(TAKYON_LIBS) -lTakyon
    ifeq ($(USE_STATIC_LIB),Yes)
        PRJ_LIBS := -L$(TAKYON_LIBS) -lTakyonStatic
    endif
endif

# Check for CUDA integration
ifeq ($(WITH_CUDA),Yes)
    CFLAGS += -DWITH_CUDA -I/usr/local/cuda/include
    PRJ_LIBS += -L/usr/local/cuda/lib64 -lcudart -lcuda
endif

vpath %.c .
vpath %.c ../../extensions

all: $(TARGET)

clean:
	rm -f *.o
	rm -f *~
	rm -f $(TARGET)

$(C_OBJS): %.o: %.c
	gcc $(CFLAGS) -c $< -o $@

$(TARGET): $(C_OBJS)
	gcc $(LDFLAGS) -o $@ $(C_OBJS) $(PRJ_LIBS)
