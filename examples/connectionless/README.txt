Description:
------------
  Shows how to use Takyon with an unconneted communication path. This allows for
  unreliable transfers; i.e. messages may be dropped or received out of order.
  This is ideal for data transfers where data integrity is not critical, e.g.
  live audio/video streams, Lidars, GigE cameras, A2D/D2A devices, and many
  other IO devices.

  In this example, each endpoint is independent of each other; i.e. you can use
  control-C to stop one endpoint and then restart it, and the other endpoint will
  be uneffected. You could even connect with an outside source to test live
  streaming or some IO device.

  In this example, each endpoint is a Takyon endpoint, but the endpoint's don't
  know or care that the remote side is a Takyon endpoint.

  This application supports both unconnected point to point and multicast where
  one sender simultaneously sends to multiple destinations.


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
      > ./connectionless

    Unicast experiments:
      Terminal 1: > ./connectionless "UnicastSendSocket -IP=127.0.0.1 -port=12345" -endpointA
      Terminal 2: > ./connectionless "UnicastRecvSocket -IP=127.0.0.1 -port=12345 -reuse"

    Multicast experiments:
      Terminal 1: > ./connectionless "MulticastSendSocket -IP=127.0.0.1 -group=239.1.2.3 -port=12345" -endpointA
      Terminal 2: > ./connectionless "MulticastRecvSocket -IP=127.0.0.1 -group=239.1.2.3 -port=12345 -reuse"

  Windows:
    Follow the same as above, but replace "./connectionless" with "connectionless"
