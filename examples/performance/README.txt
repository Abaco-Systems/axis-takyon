Description:
------------
  This example is designed to show performance (latency and throughput) of any
  supported interconnect.

  This example can be run either as multi-threaded (where the threads are explicitly
  created) or as multi-process (here the executable must be run from two command
  shells).

  There are many supported command line arguments to tune the performance testing
  as needed. To see all the supported options, just run the executable without
  any arguments.


Build:
------
  Mac and Linux:
    Terminal:
      > make
      > make USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    DOS Shell:
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./performance

    Multi threaded (test variations that include both endpoints):
      > ./performance "InterThreadMemcpy -ID=1" -mt
      > ./performance "InterThreadMemcpy -ID=1" -mt -poll
      > ./performance "InterThreadPointer -ID=1" -mt
      > ./performance "InterThreadPointer -ID=1" -mt -poll

    Multi process:
      Terminal 1: > ./performance "InterProcessMemcpy -ID=1" -endpointA
      Terminal 2: > ./performance "InterProcessMemcpy -ID=1"

      Terminal 1: > ./performance "InterProcessMemcpy -ID=1" -endpointA -poll
      Terminal 2: > ./performance "InterProcessMemcpy -ID=1" -poll

      Terminal 1: > ./performance "InterProcessPointer -ID=1" -endpointA
      Terminal 2: > ./performance "InterProcessPointer -ID=1"

      Terminal 1: > ./performance "InterProcessPointer -ID=1" -endpointA -poll
      Terminal 2: > ./performance "InterProcessPointer -ID=1" -poll

      Terminal 1: > ./performance "InterProcessSocket -ID=1" -endpointA
      Terminal 2: > ./performance "InterProcessSocket -ID=1"

      Terminal 1: > ./performance "InterProcessSocket -ID=1" -endpointA -poll
      Terminal 2: > ./performance "InterProcessSocket -ID=1" -poll

      Terminal 1: > ./performance "Socket -client -IP=127.0.0.1 -port=12345" -endpointA
      Terminal 2: > ./performance "Socket -server -IP=127.0.0.1 -port=12345 -reuse"

      Terminal 1: > ./performance "Socket -client -IP=127.0.0.1 -ID=1" -endpointA -poll
      Terminal 2: > ./performance "Socket -server -IP=Any -ID=1" -poll

  Windows:
    Follow the same as above, but replace "./performance" with "performance"
