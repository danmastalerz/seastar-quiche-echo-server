#ifndef SEASTAR_QUICHE_ECHO_DEMO_SERVER_H
#define SEASTAR_QUICHE_ECHO_DEMO_SERVER_H

#include <quiche.h>
#include <quiche_utils.h>
#include <quic_connection/quic_connection.h>


#include <seastar/core/reactor.hh>

using namespace seastar::net;

class Server {
private:
    udp_channel channel;
    quiche_config *config;
    std::map<std::vector<uint8_t>, quic_connection_ptr> clients;
    uint8_t receive_buffer[MAX_DATAGRAM_SIZE]{};
    ssize_t receive_len;
    seastar::future<> udp_send_queue;

    static int read_header_info(uint8_t *buf, size_t buf_size, quic_header_info *info);
    seastar::future<> handle_datagram(udp_datagram &dgram);
    seastar::future<> send_data();

    seastar::future<> handle_pre_hs_connection(struct quic_header_info *info, udp_datagram &datagram);
    seastar::future<> negotiate_version(struct quic_header_info *info, udp_datagram &datagram);
    seastar::future<> quic_retry(struct quic_header_info *info, udp_datagram &datagram);

    seastar::future<> handle_post_hs_connection(quic_connection_ptr &connection, udp_datagram &datagram);

public:
    explicit Server(std::uint16_t port);

    void server_setup_config(std::string &cert, std::string &key);
    seastar::future<> service_loop();

};

#endif //SEASTAR_QUICHE_ECHO_DEMO_SERVER_H
