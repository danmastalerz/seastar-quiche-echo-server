#ifndef SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_CLIENT_SOCKET_H
#define SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_CLIENT_SOCKET_H


#include <quiche.h>
#include <quiche_utils.h>

#include <seastar/core/reactor.hh>
#include <seastar/core/timer.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/app-template.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/core/distributed.hh>
#include <seastar/core/print.hh>
#include <iostream>
#include <utility>
#include <seastar/core/sleep.hh>
#include <client/client.h>

using namespace seastar::net;
using namespace std::chrono_literals;


class connected_client_socket {
private:
    Client client;

public:
    explicit connected_client_socket(const char *host, std::uint16_t port) :
            client(host, port) {}

    seastar::future<> write_to_input_stream(const std::string &msg, size_t written) {
        return client.write_input(msg, written);
    }

    seastar::future<seastar::temporary_buffer<char>> read_from_output_stream() {
        return client.read_output();
    }

    seastar::future<> connect() {
        client.client_setup_config();
        std::cerr << "Running client loop...\n";
        return client.initialize();
    }

    seastar::future<> loop() {
        std::cerr << "Keep...\n";
        return client.client_loop();
    }

    // random data-sending example
    seastar::future<> write() {
        std::string message = "data1";
        size_t msg_size = message.size() + 1;
        return write_to_input_stream(message, msg_size).then([this] {
            return seastar::keep_doing([this]() {
                std::cerr << "Writing..." << std::endl;
                std::string message2 = "data2";
                size_t msg_size2 = message2.size() + 1;
                return write_to_input_stream(message2, msg_size2);
            });
        });
    }
};

#endif //SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_CLIENT_SOCKET_H
