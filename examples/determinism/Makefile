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


APP_NAME       := determinism
COMMON_FLAGS   := -std=gnu99 -Wall -Werror -pthread
#COMMON_FLAGS   += -g -O0
COMMON_FLAGS   += -O2
CFLAGS         := $(COMMON_FLAGS) -I../../API/inc -I../../extensions
LDFLAGS        := $(COMMON_FLAGS)
C_OBJS         := determinism.o takyon_time.o takyon_attributes.o
TARGET         := $(APP_NAME)

ifeq ($(OS),Windows_NT)
    CCFLAGS += -D WIN32
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
	PRJ_LIBS := -rdynamic -Wl,--whole-archive -L../../API/builds/linux_intel_64bit -lTakyon -Wl,--no-whole-archive -ldl -lrt -lm
        ifeq ($(USE_STATIC_LIB),Yes)
	    PRJ_LIBS := -L../../API/builds/linux_intel_64bit -lTakyonStatic -lrt -lm
        endif
    endif
    ifeq ($(UNAME_S),Darwin)
	PRJ_LIBS := -rdynamic -all_load -L../../API/builds/mac_intel_64bit -lTakyon
        ifeq ($(USE_STATIC_LIB),Yes)
	    PRJ_LIBS := -L../../API/builds/mac_intel_64bit -lTakyonStatic
        endif
    endif
endif

# Run with valgrind: valgrind -v --leak-check=yes ./determinism "Memcpy -ID 0" -mt
# Run with valgrind: valgrind -v --leak-check=full --show-leak-kinds=all ./determinism "Memcpy -ID 0" -mt

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
