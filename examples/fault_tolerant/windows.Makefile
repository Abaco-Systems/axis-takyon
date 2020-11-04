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


TARGET       = fault_tolerant.exe
C_OBJS       = fault_tolerant.obj takyon_time.obj takyon_attributes.obj

# Verify TAKYON_LIBS is set
!IF "$(TAKYON_LIBS)" == ""
!  ERROR 'The environment variable TAKYON_LIBS is not defined!'
!ENDIF

# Check for CUDA integration
!IF "$(WITH_CUDA)" == "Yes"
!  IF "$(CUDA_HOME)" == ""
!    ERROR 'CUDA_HOME environment variable not defined!'
!  ENDIF
CUDA_CFLAGS = -DWITH_CUDA -I"$(CUDA_HOME)\include"
CUDA_LIB = "$(CUDA_HOME)\lib\x64\cudart.lib"
!  MESSAGE Enabling CUDA integration:
!  MESSAGE CUDA_CFLAGS = $(CUDA_CFLAGS)
!  MESSAGE CUDA_LIB = $(CUDA_LIB)
!  MESSAGE
!ELSE
CUDA_CFLAGS =
CUDA_LIB =
!ENDIF

#PTHREADS_INCLUDE = -I../../API/inc/windows
PTHREADS_INCLUDE = -Ic:/pthreads4w/install/include
CFLAGS       = -O2 -MD -W3 -WX -nologo -Zm200 -Zc:wchar_t- -D_CRT_SECURE_NO_WARNINGS=1 -I../../API/inc $(CUDA_CFLAGS) $(PTHREADS_INCLUDE) -I../../extensions
LDFLAGS      = /NOLOGO /INCREMENTAL:NO /MANIFEST:embed /SUBSYSTEM:console
PTHREADS_LIB = c:/pthreads4w/install/lib/libpthreadVC3.lib /NODEFAULTLIB:LIBCMT.LIB
#PTHREADS_LIB = c:/pthreads4w/install/lib/libpthreadVC3d.lib /NODEFAULTLIB:LIBCMT.LIB
LIBS         = $(TAKYON_LIBS)/TakyonStatic.lib Ws2_32.lib $(CUDA_LIB) $(PTHREADS_LIB)

.SUFFIXES: .c

all: $(TARGET)

{.\}.c{}.obj::
	cl -c $(CFLAGS) -Fo $<

{..\..\extensions\}.c{}.obj::
	cl -c $(CFLAGS) -Fo $<

$(TARGET): $(C_OBJS)
	link $(LDFLAGS) /OUT:$(TARGET) $(C_OBJS) $(LIBS)

clean:
	-del *~ *.obj *.pdb $(TARGET)
