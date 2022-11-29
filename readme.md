# Echo server with Quiche in Seastar

## Usage
```
sudo ./a.out
```
This will run the server on the address: 127.0.0.1:1234.

Apart from that, it works exactly the same as echo-server in quiche, but uses Seastar event loop and Seastar networking.
There's script called "build.sh" with which I've been compilling the code, you can modify it and specify your own file for quiche library.  

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
