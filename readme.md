# Echo server with Quiche in Seastar

## Usage
```
sudo ./a.out
```
This will run the server on the address: 127.0.0.1:1234.

Apart from that, it works exactly the same as echo-server in quiche, but uses Seastar event loop and Seastar networking.
There's script called "build.sh" with which I've been compilling the code, you can modify it and specify your own file for quiche library.
