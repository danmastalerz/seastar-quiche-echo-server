# Echo server with Quiche in Seastar
## Cmake
Specify environment variable `QUICHE_LIB_HOME` pointing to home directory of quiche located on your machine.
```
export QUICHE_LIB_HOME=<path_to_quiche>
```
Then simply run:
```
mkdir build
cd build
cmake ..
make
```
  

`NOTE`: One may also provide path to fmt library version 8.x.x in `FMT_V8_LIB_HOME` environment variable, but it's not mandatory (if you have this version of library installed to your system).

## Running
Note that each argument wrapped in `[]` is optional. By default `port` is 1234.
```
sudo ./echo_server [--port <listen_port>] [--cert <path_to_certificate>] [--key <path_to_key>]
sudo ./echo_client [--port <server_port>]
```
