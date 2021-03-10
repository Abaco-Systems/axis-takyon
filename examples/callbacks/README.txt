Description:
------------
  By design, Takyon does NOT use callbacks to inform the application of when a send or recv
  transfer is complete. This decision was made because callbacks are not generally used for
  communication in lower level designs (e.g. sockets, RDMA) since callbacks require either
  multi threading or interrupt handlers. This does not stop a communication API from having
  a higher level callback based wrapper. Many applications are designed with multi threading,
  and therefor would be well designed for callbacks.

  This example is designed to show how Takyon can be wrapped in a higher level layer to allow
  for callbacks.


Build:
------
  Mac and Linux:
    Terminal:
      > export TAKYON_LIBS=<folder>  // e.g. $HOME/Takyon/API/builds/linux
      > make
      > make USE_STATIC_LIB=Yes      // Use static Takyon lib to avoid dynamic Takyon libs
  Windows:
    DOS Shell:
      > set TAKYON_LIBS=<folder>     // e.g. c:\takyon\API\build\windows
      > nmake -f windows.Makefile


Run:
----
  Mac and Linux:
    To see all the usage options:
      > ./callbacks

    Multi threaded (test variations that include both endpoints):
      > ./callbacks "InterThreadMemcpy -ID=1" -mt
      > ./callbacks "InterThreadMemcpy -ID=1" -mt -poll
      > ./callbacks "InterThreadPointer -ID=1" -mt
      > ./callbacks "InterThreadPointer -ID=1" -mt -poll

    Multi process:
      
      Terminal 1: > ./callbacks "InterProcessMemcpy -ID=1" -endpointA
      Terminal 2: > ./callbacks "InterProcessMemcpy -ID=1"

      Terminal 1: > ./callbacks "InterProcessMemcpy -ID=1" -endpointA -poll
      Terminal 2: > ./callbacks "InterProcessMemcpy -ID=1" -poll

      Terminal 1: > ./callbacks "InterProcessPointer -ID=1" -endpointA
      Terminal 2: > ./callbacks "InterProcessPointer -ID=1"

      Terminal 1: > ./callbacks "InterProcessPointer -ID=1" -endpointA -poll
      Terminal 2: > ./callbacks "InterProcessPointer -ID=1" -poll

      Terminal 1: > ./callbacks "InterProcessSocket -ID=1" -endpointA
      Terminal 2: > ./callbacks "InterProcessSocket -ID=1"

      Terminal 1: > ./callbacks "InterProcessSocket -ID=1" -endpointA -poll
      Terminal 2: > ./callbacks "InterProcessSocket -ID=1" -poll

      Terminal 1: > ./callbacks "Socket -client -IP=<server_ip_addr> -port=12345" -endpointA
      Terminal 2: > ./callbacks "Socket -server -IP=<local_ip_addr> -port=12345 -reuse"

      Terminal 1: > export TAKYON_MULTICAST_IP=<local_ip_addr>
                  > ./callbacks "Socket -client -IP=<server_ip_addr> -ID=1" -endpointA -poll
      Terminal 2: > export TAKYON_MULTICAST_IP=<local_ip_addr>
                  > ./callbacks "Socket -server -IP=Any -ID=1" -poll

  Windows:
    Follow the same as above, but replace "./callbacks" with "callbacks"
