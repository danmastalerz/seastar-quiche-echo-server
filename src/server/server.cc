#include <server/server.h>
#include <cinttypes>


Server::Server(std::uint16_t port) :
        channel(seastar::make_udp_channel(port)),
        config(nullptr),
        clients(),
        receive_buffer(),
        receive_len() {}


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
            auto fragment_array = dgram.get_data().fragment_array();
            memcpy(receive_buffer, fragment_array->base, fragment_array->size);
            receive_len = dgram.get_data().len();
            receive_buffer[receive_len] = '\0';
            std::cerr << "Received " << receive_len << " bytes from socket\n";

            // Feed the raw data into quiché and handle the connection
            return handle_datagram(dgram);
        });
    });

}


seastar::future<> Server::handle_datagram(udp_datagram &dgram) {

    struct quic_header_info header_info{};

    int rc = read_header_info(receive_buffer, receive_len, &header_info);
    if (rc < 0) {
        fprintf(stderr, "failed to parse header: %d\n", rc);
        return seastar::make_ready_future<>();
    }
    
    std::vector<uint8_t> map_key(header_info.dcid, header_info.dcid + header_info.dcid_len);
    if (clients.find(map_key) == clients.end()) {
        std::cerr << "NON_ESTABLISHED_CONNECTION...\n";
        return handle_pre_hs_connection(&header_info, dgram);
    }

    quic_connection_ptr connection = clients[map_key];
    return handle_post_hs_connection(connection, dgram);
}


int Server::read_header_info(uint8_t *buf, size_t buf_size, quic_header_info *info) {
    return quiche_header_info(buf, buf_size, LOCAL_CONN_ID_LEN, &info->version,
                              &info->type, info->scid, &info->scid_len, info->dcid,
                              &info->dcid_len, info->token, &info->token_len);
}


seastar::future<> Server::handle_pre_hs_connection(struct quic_header_info *info, udp_datagram &datagram) {


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

    std::optional<quic_connection_ptr> connection_opt = QuicConnection::from(info, &local_addr, local_addr_len,
                                                                             peer_addr, peer_addr_len, config);

    if (!connection_opt) {
        return seastar::make_ready_future<>();
    }
    
    clients.emplace(connection_opt.value()->get_connection_id(), connection_opt.value());
    return handle_post_hs_connection(connection_opt.value(), datagram);
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

    std::cerr << "sending " << written << " bytes.\n";
    return channel.send(datagram.get_src(), seastar::temporary_buffer<char>(out, written));
}


seastar::future<> Server::quic_retry(quic_header_info *info, udp_datagram &datagram) {
    static char out[MAX_DATAGRAM_SIZE];

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

    std::cerr << "sending " << written << " bytes.\n";
    return channel.send(datagram.get_src(), seastar::temporary_buffer<char>(out, written));
}


seastar::future<> Server::handle_post_hs_connection(quic_connection_ptr &connection, udp_datagram &datagram) {
    return seastar::do_with(std::move(connection), std::move(datagram),
            [this] (quic_connection_ptr &connection, udp_datagram &datagram) {
        return connection->receive_packet(receive_buffer, receive_len, datagram).then(
                [this, &connection, &datagram] () {
                    return connection->read_from_streams_and_echo().then([this, &connection, &datagram] () {
                        return send_data(connection, datagram);
                    });
        });
    });
}


seastar::future<> Server::send_data(quic_connection_ptr &connection, udp_datagram &datagram) {
    return seastar::repeat([this, &connection, &datagram] () {
        uint8_t out[MAX_DATAGRAM_SIZE];
        quiche_send_info send_info;

        ssize_t written = quiche_conn_send(connection->get_conn(), out, sizeof(out), &send_info);

        if (written == QUICHE_ERR_DONE) {
            return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
        }

        if (written < 0) {
            std::cerr << "failed to create packet: " << written << "\n";
        }

        std::unique_ptr<char> p(new char[written]);
        std::memcpy(p.get(), out, written);
        seastar::future<> &udp_send_queue = connection->get_send_queue();

        udp_send_queue = udp_send_queue.then([this, &datagram, p = std::move(p), written] () {
            std::cerr << "sending " << written << " bytes of data.\n";
            return channel.send(datagram.get_src(), seastar::temporary_buffer<char>(p.get(), written));
        });

        return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
    });
}
