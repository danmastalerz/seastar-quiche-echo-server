#include <server/server.h>
#include <cinttypes>

void setup_config(quiche_config **config, const std::string &cert, const std::string &key) {
    *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (*config == NULL) {
        fprintf(stderr, "failed to create config\n");
        exit(1);
    }

    quiche_config_load_cert_chain_from_pem_file(*config, cert.c_str());
    quiche_config_load_priv_key_from_pem_file(*config, key.c_str());

    quiche_config_set_application_protos(*config,
                                         (uint8_t *) "\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9", 38);

    quiche_config_set_max_idle_timeout(*config, 5000);
    quiche_config_set_max_recv_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_send_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(*config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(*config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(*config, 1000000);
    quiche_config_set_initial_max_streams_bidi(*config, 100);
    quiche_config_set_cc_algorithm(*config, QUICHE_CC_RENO);

    if (*config == NULL) {
        std::cout << "Failed to create quiche confiassag" << std::endl;
    }
}

Server::Server(std::uint16_t port) :
        channel(seastar::make_udp_channel(port)),
        config(nullptr),
        clients() {}

void Server::server_setup_config(std::string &cert, std::string &key) {
    setup_config(&config, cert, key);
}

seastar::future<> Server::service_loop() {
    if (config == nullptr) {
        std::cerr << "Failed to setup quiche configuration...";
        return seastar::make_ready_future<>();
    }
    std::cerr << "Config setup successful.\n";
    std::cerr << "Listen address: " << channel.local_address() << "\n";

    return seastar::keep_doing([this] () {
        return channel.receive().then([this](udp_datagram dgram) {
            // Convert seastar udp datagram into raw data
            uint8_t buffer[MAX_DATAGRAM_SIZE];
            auto fragment_array = dgram.get_data().fragment_array();
            memcpy(buffer, fragment_array->base, fragment_array->size);

            // Feed the raw data into quiché and handle the connection
            return handle_connection(buffer, fragment_array->size, channel, dgram);
        });
    });

}

seastar::future<> Server::handle_connection(uint8_t *buf, ssize_t read, udp_channel &chan, udp_datagram &dgram) {
    struct conn_io *conn_io = nullptr;

    struct quic_header_info header_info{};

    sockaddr addr = dgram.get_src().as_posix_sockaddr();
    socklen_t addr_len = sizeof(addr);

    auto* peer_addr = (struct sockaddr_storage*) &addr;
    socklen_t peer_addr_len = addr_len;

    sockaddr local_addr = dgram.get_dst().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);

    int rc = read_header_info(buf, read, &header_info);
    if (rc < 0) {
        fprintf(stderr, "failed to parse header: %d\n", rc);
        return seastar::make_ready_future<>();
    }
    std::vector<uint8_t> map_key(header_info.dcid, header_info.dcid + header_info.dcid_len);

    if (clients.find(map_key) == clients.end()) {
        std::cerr << "NON_ESTABLISHED_CONNECTION...\n";
        return handle_non_established_connection(&header_info, dgram);
    }

    conn_io = clients[map_key];

    quiche_recv_info recv_info = {
            (struct sockaddr *) peer_addr,
            peer_addr_len,
            (struct sockaddr *) &local_addr,
            local_addr_len,
    };

    ssize_t done = quiche_conn_recv(conn_io->conn, buf, read, &recv_info);
    if (done < 0) {
        std::cerr << "failed to process packet: " << done << "\n";
        return seastar::make_ready_future<>();
    }

    if (quiche_conn_is_established(conn_io->conn)) {
        uint64_t s = 0;

        quiche_stream_iter *readable = quiche_conn_readable(conn_io->conn);

        while (quiche_stream_iter_next(readable, &s)) {
            fprintf(stderr, "stream %" PRIu64 " is readable\n", s);

            bool fin = false;
            ssize_t recv_len = quiche_conn_stream_recv(conn_io->conn, s,
                                                       buf, sizeof(buf),
                                                       &fin);

            if (recv_len < 0) {
                break;
            }

            fprintf(stderr, "Received: %s\n", buf);

            quiche_conn_stream_send(conn_io->conn, s, buf, recv_len, false);

            if (fin) {
                static const char *resp = "Stream finished.\n";
                quiche_conn_stream_send(conn_io->conn, s, (uint8_t *) resp,
                                        5, true);
            }
        }

        quiche_stream_iter_free(readable);
    }

    return send_data(conn_io, chan, dgram);
}


int Server::read_header_info(uint8_t *buf, size_t buf_size, quic_header_info *info) {
    return quiche_header_info(buf, buf_size, LOCAL_CONN_ID_LEN, &info->version,
                              &info->type, info->scid, &info->scid_len, info->dcid,
                              &info->dcid_len, info->token, &info->token_len);
}


seastar::future<> Server::handle_non_established_connection(struct quic_header_info *info, udp_datagram &datagram) {


    if (!quiche_version_is_supported(info->version)) {
        std::cerr << "NEGOTIATING VERSION...\n";
        return negotiate_version(info, datagram);
    }

    if (info->token_len == 0) {
        std::cerr << "QUIC_RETRY...\n";
        return quic_retry(info, datagram);
    }

    sockaddr addr = datagram.get_src().as_posix_sockaddr();
    socklen_t addr_len = sizeof(addr);

    auto* peer_addr = (struct sockaddr_storage*) &addr;
    socklen_t peer_addr_len = addr_len;

    sockaddr local_addr = datagram.get_dst().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);

    if (!validate_token(info->token, info->token_len, peer_addr, peer_addr_len,
                        info->odcid, &info->odcid_len)) {
        std::cerr << "invalid address validation token\n";
        return seastar::make_ready_future<>();
    }

    struct conn_io *conn_io = create_conn(
            info->dcid, info->dcid_len, info->odcid,
            info->odcid_len, &local_addr, local_addr_len,
            peer_addr, peer_addr_len, config, clients
    );

    if (conn_io == nullptr) {
        std::cout << "failed to create connection\n";
    }

    return seastar::make_ready_future<>();
}

seastar::future<> Server::negotiate_version(struct quic_header_info *info, udp_datagram &datagram) {

    char out[MAX_DATAGRAM_SIZE];

    ssize_t written = quiche_negotiate_version(info->scid, info->scid_len,
                                               info->dcid, info->dcid_len,
                                               reinterpret_cast<uint8_t *>(out), sizeof(out));

    if (written < 0) {
        std::cerr << "failed to create vneg packet: " << written << "\n";
        return seastar::make_ready_future<>();
    }

    return channel.send(datagram.get_src(), seastar::temporary_buffer<char>(out, written));
}


seastar::future<> Server::quic_retry(quic_header_info *info, udp_datagram &datagram) {
    char out[MAX_DATAGRAM_SIZE];

    sockaddr addr = datagram.get_src().as_posix_sockaddr();
    socklen_t addr_len = sizeof(addr);

    auto* peer_addr = (struct sockaddr_storage*) &addr;
    socklen_t peer_addr_len = addr_len;

    mint_token(info->dcid, info->dcid_len, peer_addr, peer_addr_len,
               info->token, &info->token_len);

    uint8_t new_cid[LOCAL_CONN_ID_LEN];

    if (gen_cid(new_cid, LOCAL_CONN_ID_LEN) == nullptr) {
        return seastar::make_ready_future<>();
    }

    ssize_t written = quiche_retry(info->scid, info->scid_len,
                                   info->dcid, info->dcid_len,
                                   new_cid, LOCAL_CONN_ID_LEN,
                                   info->token, info->token_len,
                                   info->version, reinterpret_cast<uint8_t *>(out), sizeof(out));

    if (written < 0) {
        std::cerr << "failed to create retry packet: " << written << "\n";
        return seastar::make_ready_future<>();
    }

    return channel.send(datagram.get_src(), seastar::temporary_buffer<char>(out, written));
}

seastar::future<> Server::send_data(struct conn_io *conn_data, udp_channel &chan, udp_datagram &dgram) {
    uint8_t out[MAX_DATAGRAM_SIZE];

    quiche_send_info send_info;

    while (true) {
        ssize_t written = quiche_conn_send(conn_data->conn, out, sizeof(out),
                                           &send_info);

        if (written == QUICHE_ERR_DONE) {
            break;
        }

        if (written < 0) {
            fprintf(stderr, "failed to create packet: %zd\n", written);
            exit(1);
        }

        (void) chan.send(dgram.get_src(),
                         seastar::temporary_buffer<char>(reinterpret_cast<const char *>(out), written));

    }

    return seastar::make_ready_future<>();
}