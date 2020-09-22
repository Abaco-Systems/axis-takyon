Description:
------------
  Shows how to use Takyon with a one sided communication path. I.e. assume one
  side is some well defined 3rd party endpoint, such as a Lidar device sending
  out its point cloud data on a TCP server socket using a well known port number and
  IP address.

  In this example, each endpoint is a Takyon endpoint, but the endpoint's don't
  know or care that the remote side is a Takyon endpoint.


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
    To see the usage options:
      > ./one_sided

    Test variations:
      Terminal 1: > ./one_sided "OneSidedSocket -client -IP=127.0.0.1 -port=12345" -endpointA
      Terminal 2: > ./one_sided "OneSidedSocket -server -IP=127.0.0.1 -port=12345 -reuse"

  Windows:
    Follow the same as above, but replace "./one_sided" with "one_sided"
