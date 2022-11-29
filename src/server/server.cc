#include <server/server.h>
#include <cinttypes>

Server::Server(std::uint16_t port) :
        channel(seastar::make_udp_channel(port)),
        config(nullptr),
        clients() {}

void Server::setup_config(std::string &cert, std::string &key) {
    ::setup_config(&config, cert, key);
}

seastar::future<> Server::service_loop() {
    if (config == nullptr) {
        std::cerr << "Failed to setup quiche configuration...";
        return seastar::make_ready_future<>();
    }

    return channel.receive().then([this](udp_datagram dgram) {
        // Convert seastar udp datagram into raw data
        uint8_t buffer[MAX_DATAGRAM_SIZE];
        auto fragment_array = dgram.get_data().fragment_array();
        memcpy(buffer, fragment_array->base, fragment_array->size);

        // Feed the raw data into quiché and handle the connection
        return handle_connection(buffer, fragment_array->size, channel, dgram);

    });
}

seastar::future<> Server::handle_connection(uint8_t *buf, ssize_t read, udp_channel &chan, udp_datagram &dgram) {
    struct conn_io *conn_io = NULL;

    static char out[MAX_DATAGRAM_SIZE];


    sockaddr addr = dgram.get_src().as_posix_sockaddr();
    socklen_t addr_len = sizeof(addr);


    struct sockaddr_storage* peer_addr = (struct sockaddr_storage*) &addr;
    socklen_t peer_addr_len = addr_len;


    sockaddr local_addr = dgram.get_dst().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);


    uint8_t type;
    uint32_t version;

    uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
    size_t scid_len = sizeof(scid);

    uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
    size_t dcid_len = sizeof(dcid);

    uint8_t odcid[QUICHE_MAX_CONN_ID_LEN];
    size_t odcid_len = sizeof(odcid);

    uint8_t token[MAX_TOKEN_LEN];
    size_t token_len = sizeof(token);
    int rc = quiche_header_info(buf, read, LOCAL_CONN_ID_LEN, &version,
                                &type, scid, &scid_len, dcid, &dcid_len,
                                token, &token_len);
    if (rc < 0) {
        fprintf(stderr, "failed to parse header: %d\n", rc);
        return seastar::make_ready_future<>();
    }
    std::vector<uint8_t> map_key(dcid, dcid + dcid_len);
    if (clients.find(map_key) == clients.end()) {
        if (!quiche_version_is_supported(version)) {

            ssize_t written = quiche_negotiate_version(scid, scid_len,
                                                       dcid, dcid_len,
                                                       reinterpret_cast<uint8_t *>(out), sizeof(out));

            if (written < 0) {
                fprintf(stderr, "failed to create vneg packet: %zd\n",
                        written);
                return seastar::make_ready_future<>();
            }

            return chan.send(dgram.get_src(), seastar::temporary_buffer<char>(out, written));
        }

        if (token_len == 0) {


            mint_token(dcid, dcid_len, peer_addr, peer_addr_len,
                       token, &token_len);

            uint8_t new_cid[LOCAL_CONN_ID_LEN];

            if (gen_cid(new_cid, LOCAL_CONN_ID_LEN) == nullptr) {
                return seastar::make_ready_future<>();
            }

            ssize_t written = quiche_retry(scid, scid_len,
                                           dcid, dcid_len,
                                           new_cid, LOCAL_CONN_ID_LEN,
                                           token, token_len,
                                           version, reinterpret_cast<uint8_t *>(out), sizeof(out));

            if (written < 0) {
                fprintf(stderr, "failed to create retry packet: %zd\n",
                        written);
                return seastar::make_ready_future<>();
            }

            return chan.send(dgram.get_src(), seastar::temporary_buffer<char>(out, written));
        }


        if (!validate_token(token, token_len, peer_addr, peer_addr_len,
                            odcid, &odcid_len)) {
            fprintf(stderr, "invalid address validation token\n");
            return seastar::make_ready_future<>();
        }

        conn_io = create_conn(dcid, dcid_len, odcid, odcid_len,
                              &local_addr, local_addr_len,
                              peer_addr, peer_addr_len, config, clients);

        if (conn_io == NULL) {
            std::cout << "failed to create connection\n";
            return seastar::make_ready_future<>();
        }
    }
    else {
        conn_io = clients[map_key];
    }
    quiche_recv_info recv_info = {
            (struct sockaddr *) peer_addr,
            peer_addr_len,
            (struct sockaddr *) &local_addr,
            local_addr_len,
    };
    ssize_t done = quiche_conn_recv(conn_io->conn, buf, read, &recv_info);
    if (done < 0) {
        fprintf(stderr, "failed to process packet: %zd\n", done);
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
}