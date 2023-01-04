#ifndef SEASTAR_QUICHE_ECHO_SERVER_CLIENT_H
#define SEASTAR_QUICHE_ECHO_SERVER_CLIENT_H


#include <quiche.h>
#include <quiche_utils.h>

#include <seastar/core/reactor.hh>
#include <fstream>
#include <utility>
#include <vector>
#include "stream/stream.h"

using namespace seastar::net;

class Client {
private:
    udp_channel channel;
    conn_io *connection;
    quiche_config *config;
    const char *server_host;
    seastar::ipv4_addr server_address;
    bool is_timer_active;
    std::string file;
    std::ifstream fin;
    bool are_streams_initialized;
    input input_stream;

    seastar::future<> handle_timeout();

    seastar::future<> send_data(struct conn_io *conn_data, udp_channel &chan, seastar::ipv4_addr &addr);

    seastar::future<>
    handle_connection(uint8_t *buf, ssize_t read, struct conn_io *conn_io, udp_channel &channel, udp_datagram &datagram,
                      seastar::ipv4_addr &addr);

    seastar::future<> receive();

public:
    explicit Client(const char *host, std::uint16_t port, std::string file, int core);

    void client_setup_config();

    seastar::future<> initialize();

    seastar::future<> client_loop();

    seastar::future<> pass_data(const std::vector<char>& source);

    int core;

};

#endif //SEASTAR_QUICHE_ECHO_SERVER_CLIENT_H
