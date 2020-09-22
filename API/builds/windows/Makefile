# Copyright 2018,2020 Abaco Systems
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#     http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


TAKYON_C_FILES = \
utils_arg_parser.c \
utils_endian.c \
utils_ephemeral_port_manager.c \
utils_inter_thread_manager.c \
utils_memory_windows.c \
utils_mmap_windows.c \
utils_shared_libraries.c \
utils_socket_windows.c \
utils_threads.c \
utils_time_windows.c \
takyon_InterThreadMemcpy.c \
takyon_InterThreadPointer.c \
takyon_InterProcessMemcpy.c \
takyon_InterProcessPointer.c \
takyon_InterProcessSocket.c \
takyon_Socket.c \
takyon_OneSidedSocket.c \
takyon_UnicastSendSocket.c \
takyon_UnicastRecvSocket.c \
takyon_MulticastSendSocket.c \
takyon_MulticastRecvSocket.c \
takyon.c

# Build flags
RELEASE_CFLAGS  = -O2 -MD  # -MT mean static linking, and -MD means dynamic linking.
DEBUG_CFLAGS    = -Zi -MDd # -MTd or -MDd

# HELPFUL:
# There are 4 versions of the CRT link libraries present in vc\lib:
#  - libcmt.lib:  static CRT link library for a release build (/MT)
#  - libcmtd.lib: static CRT link library for a debug build (/MTd)
#  - msvcrt.lib:  import library for the release DLL version of the CRT (/MD)
#  - msvcrtd.lib: import library for the debug DLL version of the CRT (/MDd)

#PTHREADS_INCLUDE = -I..\..\inc\windows
PTHREADS_INCLUDE = -Ic:\pthreads4w\install\include

DEBUG_LDFLAGS   = /DEBUG /FIXED:NO /NODEFAULTLIB:MSVCRT
COMMON_FLAGS    = -nologo -Zm200 -D_CRT_SECURE_NO_WARNINGS=1 -WX -W3 -Zc:wchar_t- -w34189 -GR -EHsc
CFLAGS          = $(RELEASE_CFLAGS) $(COMMON_FLAGS) -DBUILD_STATIC_LIB -I..\..\inc $(PTHREADS_INCLUDE)
TAKYON_C_OBJS   = $(TAKYON_C_FILES:.c=.obj)
LIBRARY         = TakyonStatic.lib

all: $(LIBRARY)

clean:
	del *.obj
	del *~
	del $(LIBRARY)

# Static library rule
$(LIBRARY): $(TAKYON_C_OBJS)
	lib /NOLOGO /OUT:$(LIBRARY) $(TAKYON_C_OBJS)

# C object rules
.SUFFIXES: .c

{..\..\src\}.c.obj::
	cl -c $(CFLAGS) -Fo.\ $<
