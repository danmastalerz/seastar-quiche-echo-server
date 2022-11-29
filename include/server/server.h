#ifndef SEASTAR_QUICHE_ECHO_DEMO_SERVER_H
#define SEASTAR_QUICHE_ECHO_DEMO_SERVER_H

#include <quiche.h>
#include <quiche_utils.h>

#include <seastar/core/reactor.hh>

using namespace seastar::net;

class Server {
private:
    udp_channel channel;
    quiche_config *config;
    std::map<std::vector<uint8_t>, conn_io *> clients;

    seastar::future<> handle_connection(uint8_t *buf, ssize_t read, udp_channel &chan, udp_datagram &dgram);
    seastar::future<> send_data(struct conn_io *conn_data, udp_channel &chan, udp_datagram &dgram);

public:
    Server(std::uint16_t port);

    void setup_config(std::string &cert, std::string &key);
    seastar::future<> service_loop();

};

#endif //SEASTAR_QUICHE_ECHO_DEMO_SERVER_H
