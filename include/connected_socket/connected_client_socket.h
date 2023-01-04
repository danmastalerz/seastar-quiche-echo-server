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
    explicit connected_client_socket(const char *host, std::uint16_t port, std::uint16_t shard_id, std::string file, int core) :
        client(host, port + shard_id, std::move(file), core) {}


    seastar::future<> connect() {
        client.client_setup_config();
        std::cerr << "Starting the client...\n";
        return client.initialize();
    }

    seastar::future<> loop() {
        std::cerr << "Client running...\n";
        // pass data to the client and start loop
        return client.pass_data(std::vector<char>(FILE_CHUNK)).then([this] {
             return client.client_loop();
        });
    }

    seastar::future<> pass_data(std::vector<char> source) {
        std::cerr << "Passing data...\n";
        return client.pass_data(source);
    }

};

#endif //SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_CLIENT_SOCKET_H
