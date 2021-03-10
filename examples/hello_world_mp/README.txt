Description:
------------
  A very simple example to create a single path and send messages in each direction.

  This example is exclusively done for multi-process. This example does not explicitly
  create the processes, but instead relies on the executable being run from two
  different command shells, where the command shells can be on different computers or
  the same computer. One command shell is used for endpoint A, and the other is used
  for endpoint B.

  Notice how this example is self synchronizing just by having A call send() first then
  recv(), and B calls recv() first then send().


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
    To see the usage options:
      > ./hello

    Test variations:
      Terminal 1: > ./hello "InterProcessMemcpy -ID=1" -endpointA
      Terminal 2: > ./hello "InterProcessMemcpy -ID=1"

      Terminal 1: > ./hello "InterProcessPointer -ID=1" -endpointA
      Terminal 2: > ./hello "InterProcessPointer -ID=1"

      Terminal 1: > ./hello "InterProcessSocket -ID=1" -endpointA
      Terminal 2: > ./hello "InterProcessSocket -ID=1"

      # Uses specific port number (assigned by user)
      Terminal 1: > ./hello "Socket -client -IP=<server_ip_addr> -port=12345" -endpointA
      Terminal 2: > ./hello "Socket -server -IP=<local_ip_addr> -port=12345 -reuse"

      # Uses ephemeral port number (assigned by system)
      Terminal 1: > export TAKYON_MULTICAST_IP=<local_ip_addr>
                  > ./hello "Socket -client -IP=<server_ip_addr> -ID=1" -endpointA
      Terminal 2: > export TAKYON_MULTICAST_IP=<local_ip_addr>
                  > ./hello "Socket -server -IP=<local_ip_addr> -ID=1"

  Windows:
    Follow the same as above, but replace "./hello" with "hello"
