Description:
------------
  Shows how to use Takyon with an unconnected communication path. This allows for
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
      > export TAKYON_LIBS=<folder>  // e.g. $HOME/Takyon/API/builds/linux
      > make [USE_STATIC_LIB=Yes] [WITH_RDMA=Yes] [WITH_CUDA=Yes]
  Windows:
    DOS Shell:
      > set TAKYON_LIBS=<folder>     // e.g. c:\takyon\API\build\windows
      > nmake -f windows.Makefile

Run:
----
  Mac and Linux:
    To see the usage options:
      > ./connectionless

    Unicast experiments:
      Terminal 1: > ./connectionless "UnicastSendSocket -IP=<remote_ip_addr> -port=12345" -endpointA -poll -bytes 64000 -cyclesPerPrint 100000
      Terminal 2: > ./connectionless "UnicastRecvSocket -IP=<local_ip_addr> -port=12345 -reuse -rcvbuf=1048576" -poll -bytes 64000 -cyclesPerPrint 100000

      Terminal 1: > ./connectionless "UnicastSendRdma -remoteIP=<server_ip_addr> -port=12345" -endpointA -nbufs 5 -cyclesPerPrint 100000
      Terminal 2: > ./connectionless "UnicastRecvRdma -localIP=<server_ip_addr> -port=12345" -nbufs 5 -cyclesPerPrint 100000

    Multicast experiments:
      Terminal 1: > ./connectionless "MulticastSendSocket -IP=<local_ip_addr> -group=239.1.2.3 -port=12345" -endpointA -poll
      Terminal 2: > ./connectionless "MulticastRecvSocket -IP=<local_ip_addr> -group=239.1.2.3 -port=12345 -reuse -rcvbuf=1048576" -poll

      Terminal 1: > ./connectionless "MulticastSendRdma -IP=<local_ip_addr> -group=239.1.2.3" -endpointA -nbufs 5 -cyclesPerPrint 100000
      Terminal 2: > ./connectionless "MulticastRecvRdma -IP=<local_ip_addr> -group=239.1.2.3" -nbufs 5 -cyclesPerPrint 100000

  Windows:
    Follow the same as above, but replace "./connectionless" with "connectionless"
