#ifndef SEASTAR_QUICHE_ECHO_DEMO_SERVER_H
#define SEASTAR_QUICHE_ECHO_DEMO_SERVER_H

#include <quiche.h>
#include <quiche_utils.h>

#include <seastar/core/reactor.hh>
#include <seastar/net/api.hh>

namespace zpp {

struct quic_header_info {
    std::uint8_t type{};
    std::uint32_t version{};

    std::uint8_t scid[QUICHE_MAX_CONN_ID_LEN]{};
    std::size_t scid_len = sizeof(scid);

    std::uint8_t dcid[QUICHE_MAX_CONN_ID_LEN]{};
    std::size_t dcid_len = sizeof(dcid);

    std::uint8_t odcid[QUICHE_MAX_CONN_ID_LEN]{};
    std::size_t odcid_len = sizeof(odcid);

    std::uint8_t token[MAX_TOKEN_LEN]{};
    std::size_t token_len = sizeof(token);
};

class Server {
private:
    using udp_channel   = seastar::net::udp_channel;
    using udp_datagram  = seastar::net::udp_datagram;
    
    udp_channel channel;
    quiche_config *config;
    std::map<std::vector<uint8_t>, conn_io*> clients;

    static int read_header_info(uint8_t *buf, size_t buf_size, quic_header_info *info);
    seastar::future<> handle_connection(uint8_t *buf, ssize_t read, udp_channel &chan, udp_datagram &dgram);
    seastar::future<> send_data(struct conn_io *conn_data, udp_channel &chan, udp_datagram &dgram);

    seastar::future<> handle_non_established_connection(struct quic_header_info *info, udp_datagram &datagram);
    seastar::future<> negotiate_version(struct quic_header_info *info, udp_datagram &datagram);
    seastar::future<> quic_retry(struct quic_header_info *info, udp_datagram &datagram);

public:
    explicit Server(std::uint16_t port);

    void server_setup_config(std::string &cert, std::string &key);
    seastar::future<> service_loop();

};

} // namespace zpp

#endif //SEASTAR_QUICHE_ECHO_DEMO_SERVER_H
