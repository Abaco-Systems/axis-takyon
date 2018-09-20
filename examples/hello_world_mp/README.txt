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
      > make
  Windows:
    DOS Shell:
      > nmake -f windows.Makefile

Run:
----
  Mac and Linux:
    To see the usage options:
      > ./hello

    Terminal 1 (endpoint A test variations, to match with endpoint B):
      > ./hello "Mmap -ID 1" -endpointA
      > ./hello "Mmap -ID 1 -share" -endpointA
      > ./hello "Socket -local -ID 1" -endpointA
      > ./hello "Socket -client 127.0.0.1 -port 12345" -endpointA

    Terminal 2 (endpoint B test variations, to match with endpoint A):
      > ./hello "Mmap -ID 1"
      > ./hello "Mmap -ID 1 -share"
      > ./hello "Socket -local -ID 1"
      > ./hello "Socket -server 127.0.0.1 -port 12345 -reuse"

  Windows:
    Follow the same as above, but replace "./hello" with "hello"
