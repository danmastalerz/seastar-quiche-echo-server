#ifndef SEASTAR_QUICHE_ECHO_DEMO_SERVER_H
#define SEASTAR_QUICHE_ECHO_DEMO_SERVER_H

#include <quiche.h>
#include <quiche_utils.h>

#include <seastar/core/reactor.hh>

using namespace seastar::net;

struct quic_header_info {
    uint8_t type{};
    uint32_t version{};

    uint8_t scid[QUICHE_MAX_CONN_ID_LEN]{};
    size_t scid_len = sizeof(scid);

    uint8_t dcid[QUICHE_MAX_CONN_ID_LEN]{};
    size_t dcid_len = sizeof(dcid);

    uint8_t odcid[QUICHE_MAX_CONN_ID_LEN]{};
    size_t odcid_len = sizeof(odcid);

    uint8_t token[MAX_TOKEN_LEN]{};
    size_t token_len = sizeof(token);
};

class Server {
private:
    udp_channel channel;
    quiche_config *config;
    std::map<std::vector<uint8_t>, conn_io *> clients;
    uint8_t receive_buffer[MAX_DATAGRAM_SIZE]{};
    ssize_t receive_len;

    static int read_header_info(uint8_t *buf, size_t buf_size, quic_header_info *info);
    seastar::future<> handle_connection(udp_datagram &dgram);
    seastar::future<> send_data(struct conn_io *conn_data, udp_channel &chan, udp_datagram &dgram);

    seastar::future<> handle_pre_hs_connection(struct quic_header_info *info, udp_datagram &datagram);
    seastar::future<> negotiate_version(struct quic_header_info *info, udp_datagram &datagram);
    seastar::future<> quic_retry(struct quic_header_info *info, udp_datagram &datagram);

    seastar::future<> handle_post_hs_connection(struct conn_io *conn_io, udp_datagram &datagram);

public:
    explicit Server(std::uint16_t port);

    void server_setup_config(std::string &cert, std::string &key);
    seastar::future<> service_loop();

};

#endif //SEASTAR_QUICHE_ECHO_DEMO_SERVER_H
