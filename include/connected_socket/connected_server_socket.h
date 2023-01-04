#ifndef SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_SERVER_SOCKET_H
#define SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_SERVER_SOCKET_H


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
#include <server/server.h>

using namespace seastar::net;
using namespace std::chrono_literals;


class connected_client_socket {
private:
    Server server;

public:
    explicit connected_client_socket(const char *host, std::uint16_t port) :
            server(port) {}

//    seastar::future<> write_to_input_stream(const std::string &msg, size_t written) {
//        return server.write_input(msg, written);
//    }
//    seastar::future<seastar::temporary_buffer<char>> read_from_output_stream(){
//        return server.read_output();
//    }
//
//    seastar::future<> connect() {
//        client.client_setup_config();
//        std::cerr << "Running client loop...\n";
//        return client.initialize();
//    }
//
//    seastar::future<> loop() {
//        std::cerr << "Keep...\n";
//        return client.client_loop();
//    }

//    seastar::future<> write() {
//        return write_to_input_stream("abc", 4).then([this] {
//            return seastar::keep_doing([this]() {
//                auto sleep_duration = std::chrono::seconds(1);
//                return seastar::sleep(sleep_duration).then([this] {
//                    std::cerr << "Writing..." << std::endl;
//                    return write_to_input_stream("abc", 4);
//                });
//            });
//        });
//    }
};

#endif //SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_SERVER_SOCKET_H
