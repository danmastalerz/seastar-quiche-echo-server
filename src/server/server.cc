#include <cinttypes>
#include <memory>

#include <server/server.h>
#include <quiche_utils.h>
#include <logger.h>
#include <sys/socket.h>

using namespace zpp;

void setup_config(quiche_config **config, const std::string &cert, const std::string &key) {
    *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (*config == nullptr) {
        ffail("Failed to create config.");
    }

    quiche_config_load_cert_chain_from_pem_file(*config, cert.c_str());
    quiche_config_load_priv_key_from_pem_file(*config, key.c_str());

    quiche_config_set_application_protos(
        *config,
        reinterpret_cast<const uint8_t*>("\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9"),
        38
    );

    quiche_config_set_max_idle_timeout(*config, 5000);
    quiche_config_set_max_recv_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_send_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(*config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(*config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(*config, 1000000);
    quiche_config_set_initial_max_streams_bidi(*config, 100);
    quiche_config_set_cc_algorithm(*config, QUICHE_CC_RENO);

    if (*config == nullptr) {
        eflog("Failed to create quiche confiassag.");
    }
}

Server::Server(std::uint16_t port) :
        channel(seastar::make_udp_channel(port)),
        config(nullptr),
        clients() {}

void Server::server_setup_config(std::string &cert, std::string &key) {
    setup_config(std::addressof(config), cert, key);
}

seastar::future<> Server::service_loop() {
    using std::literals::string_literals::operator""s;
    
    if (config == nullptr) {
        eflog("Failed to setup quiche configuration...");
        return seastar::make_ready_future<>();
    }

    flog("Config setup successful.");
    flog("Listen address: ", channel.local_address());

    return seastar::keep_doing([this] () {
        return channel.receive().then([this](udp_datagram dgram) {
            // Convert seastar udp datagram into raw data
            std::uint8_t buffer[MAX_DATAGRAM_SIZE];
            auto fragment_array = dgram.get_data().fragment_array();
            std::memcpy(buffer, fragment_array->base, fragment_array->size);

            // Feed the raw data into quiché and handle the connection
            return handle_connection(buffer, fragment_array->size, channel, dgram);
        });
    });

}

seastar::future<> Server::handle_connection(std::uint8_t *buf, ssize_t read, udp_channel &chan, udp_datagram &dgram) {
    conn_io *conn_io = nullptr;
    quic_header_info header_info{};

    sockaddr addr = dgram.get_src().as_posix_sockaddr();
    socklen_t addr_len = sizeof(addr);

    auto peer_addr = reinterpret_cast<sockaddr_storage*>(std::addressof(addr));
    socklen_t peer_addr_len = addr_len;

    sockaddr local_addr = dgram.get_dst().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);

    int rc = read_header_info(buf, read, &header_info);
    if (rc < 0) {
        eflog("Failed to parse header: ", rc);
        return seastar::make_ready_future<>();
    }
    std::vector<uint8_t> map_key(header_info.dcid, header_info.dcid + header_info.dcid_len);

    if (clients.find(map_key) == clients.end()) {
        eflog("NON_ESTABLISHED_CONNECTION...");
        return handle_non_established_connection(&header_info, dgram);
    }

    conn_io = clients[map_key];

    quiche_recv_info recv_info = {
        reinterpret_cast<sockaddr*>(peer_addr),
        peer_addr_len,
        reinterpret_cast<sockaddr*>(std::addressof(local_addr)),
        local_addr_len,
    };

    ssize_t done = quiche_conn_recv(conn_io->conn, buf, read, &recv_info);
    if (done < 0) {
        eflog("Failed to process packet: ", done);
        return seastar::make_ready_future<>();
    }

    if (quiche_conn_is_established(conn_io->conn)) {
        std::uint64_t s = 0;

        quiche_stream_iter *readable = quiche_conn_readable(conn_io->conn);

        while (quiche_stream_iter_next(readable, &s)) {
            eflog("Stream ", s, " is readable");

            bool fin = false;
            ssize_t recv_len = quiche_conn_stream_recv(conn_io->conn, s,
                                                       buf, sizeof(buf),
                                                       &fin);

            if (recv_len < 0) {
                break;
            }

            eflog("Received: ", buf);

            quiche_conn_stream_send(conn_io->conn, s, buf, recv_len, false);

            if (fin) {
                static const char *resp = "Stream finished.\n";
                quiche_conn_stream_send(conn_io->conn, s, reinterpret_cast<const std::uint8_t*>(resp),
                                        5, true);
            }
        }

        quiche_stream_iter_free(readable);
    }

    return send_data(conn_io, chan, dgram);
}


int Server::read_header_info(std::uint8_t *buf, std::size_t buf_size, quic_header_info *info) {
    return quiche_header_info(buf, buf_size, LOCAL_CONN_ID_LEN, &info->version,
                              &info->type, info->scid, &info->scid_len, info->dcid,
                              &info->dcid_len, info->token, &info->token_len);
}


seastar::future<> Server::handle_non_established_connection(quic_header_info *info, udp_datagram &datagram) {


    if (!quiche_version_is_supported(info->version)) {
        eflog("NEGOTIATING VERSION...");
        return negotiate_version(info, datagram);
    }

    if (info->token_len == 0) {
        eflog("QUIC_RETRY...");
        return quic_retry(info, datagram);
    }

    sockaddr addr = datagram.get_src().as_posix_sockaddr();
    socklen_t addr_len = sizeof(addr);

    decltype(auto) peer_addr = reinterpret_cast<sockaddr_storage*>(std::addressof(addr));
    socklen_t peer_addr_len = addr_len;

    sockaddr local_addr = datagram.get_dst().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);

    if (!validate_token(info->token, info->token_len, peer_addr, peer_addr_len,
                        info->odcid, &info->odcid_len)) {
        eflog("Invalid address validation token");
        return seastar::make_ready_future<>();
    }

    struct conn_io *conn_io = create_conn(
            info->dcid, info->dcid_len, info->odcid,
            info->odcid_len, &local_addr, local_addr_len,
            peer_addr, peer_addr_len, config, clients
    );

    if (conn_io == nullptr) {
        eflog("Failed to create connection");
    }

    return seastar::make_ready_future<>();
}

seastar::future<> Server::negotiate_version(quic_header_info *info, udp_datagram &datagram) {
    char out[MAX_DATAGRAM_SIZE];

    ssize_t written = quiche_negotiate_version(info->scid, info->scid_len,
                                               info->dcid, info->dcid_len,
                                               reinterpret_cast<std::uint8_t*>(out), sizeof(out));

    if (written < 0) {
        eflog("Failed to create vneg packet: ", written);
        return seastar::make_ready_future<>();
    }

    return channel.send(datagram.get_src(), seastar::temporary_buffer<char>(out, written));
}


seastar::future<> Server::quic_retry(quic_header_info *info, udp_datagram &datagram) {
    char out[MAX_DATAGRAM_SIZE];

    sockaddr addr = datagram.get_src().as_posix_sockaddr();
    socklen_t addr_len = sizeof(addr);

    decltype(auto) peer_addr = reinterpret_cast<sockaddr_storage*>(std::addressof(addr));
    socklen_t peer_addr_len = addr_len;

    mint_token(info->dcid, info->dcid_len, peer_addr, peer_addr_len,
               info->token, &info->token_len);

    std::uint8_t new_cid[LOCAL_CONN_ID_LEN];

    if (gen_cid(new_cid, LOCAL_CONN_ID_LEN) == nullptr) {
        return seastar::make_ready_future<>();
    }

    ssize_t written = quiche_retry(info->scid, info->scid_len,
                                   info->dcid, info->dcid_len,
                                   new_cid, LOCAL_CONN_ID_LEN,
                                   info->token, info->token_len,
                                   info->version, reinterpret_cast<std::uint8_t*>(out), sizeof(out));

    if (written < 0) {
        eflog("Failed to create retry packet: ", written);
        return seastar::make_ready_future<>();
    }

    return channel.send(datagram.get_src(), seastar::temporary_buffer<char>(out, written));
}

seastar::future<> Server::send_data(conn_io *conn_data, udp_channel &chan, udp_datagram &dgram) {
    std::uint8_t out[MAX_DATAGRAM_SIZE];

    quiche_send_info send_info;

    while (true) {
        ssize_t written = quiche_conn_send(conn_data->conn, out, sizeof(out),
                                           &send_info);

        if (written == QUICHE_ERR_DONE) {
            break;
        }

        if (written < 0) {
            ffail("Failed to create packet: ", written);
        }

        (void) chan.send(dgram.get_src(),
                         seastar::temporary_buffer<char>(reinterpret_cast<const char*>(out), written));

    }

    return seastar::make_ready_future<>();
}
