#include <cinttypes>
#include <seastar/core/seastar.hh>
#include <seastar/core/future-util.hh>

#include <seastar/core/app-template.hh>
#include <seastar/util/log.hh>
#include <client/client.h>
#include <connected_socket/connected_socket_client.h>

int main(int argc, char **argv) {
    seastar::app_template app;

    try {
        app.run(argc, argv, [&]() {

            const char *host = "127.0.0.1";
            uint16_t port = 1234;
            std::cout << "debug\n";
            // Connect to the server.
            connected_socket_client socket(seastar::ipv4_addr(host, port));
            socket.setup_config();
            std::cout << "debug2\n";
            return socket.connect().then([]{
                std::cout << "Connected to the server.\n";
                return seastar::make_ready_future<>();
            });


            return seastar::make_ready_future<>();


        });
    } catch (...) {
        std::cerr << "Couldn't start application: "
                  << std::current_exception() << "\n";
        return 1;
    }
    return 0;
}

