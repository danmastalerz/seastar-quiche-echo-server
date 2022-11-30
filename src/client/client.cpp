#include <client/client.h>
#include <seastar/core/sleep.hh>
#include <cinttypes>

void setup_config(quiche_config **config) {

    *config = quiche_config_new(0xbabababa);
    if (*config == nullptr) {
        fprintf(stderr, "failed to create config\n");
        exit(1);
    }

    quiche_config_set_application_protos(*config,
                                         (uint8_t *) "\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9", 38);

    quiche_config_set_max_idle_timeout(*config, 5000);
    quiche_config_set_max_recv_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_send_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(*config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(*config, 1000000);
    quiche_config_set_initial_max_stream_data_uni(*config, 1000000);
    quiche_config_set_initial_max_streams_bidi(*config, 100);
    quiche_config_set_initial_max_streams_uni(*config, 100);
    quiche_config_set_disable_active_migration(*config, true);

    if (getenv("SSLKEYLOGFILE")) {
        quiche_config_log_keys(*config);
    }

}

Client::Client(const char *host, uint16_t port) :
        channel(seastar::make_udp_channel()),
        server_host(host),
        server_address(seastar::ipv4_addr(host, port)),
        is_timer_active(false),
        config(nullptr) {}

void Client::client_setup_config() {
    setup_config(&config);
}

seastar::future<> Client::client_loop() {

    std::cout << "starting client loop" << std::endl;

    if (config == nullptr) {
        std::cerr << "Failed to setup quiche configuration...";
        return seastar::make_ready_future<>();
    }
    std::cerr << "Config setup successful.\n";


    uint8_t scid[LOCAL_CONN_ID_LEN];
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        perror("failed to open /dev/urandom");
        return seastar::make_ready_future<>();
    }

    ssize_t rand_len = read(rng, &scid, sizeof(scid));
    if (rand_len < 0) {
        perror("failed to create connection ID");
        return seastar::make_ready_future<>();
    }

    struct conn_io *conn_data = nullptr;
    conn_data = (conn_io *) calloc(1, sizeof(conn_io));

    if (conn_data == nullptr) {
        fprintf(stderr, "failed to allocate connection IO\n");
        return seastar::make_ready_future<>();
    }

    sockaddr local_addr = channel.local_address().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);

    seastar::socket_address dest(server_address);

    sockaddr peer_addr = dest.as_posix_sockaddr();
    socklen_t peer_addr_len = sizeof(peer_addr);

    connection = (conn_io *) calloc(1, sizeof(conn_io));
    if (connection == nullptr) {
        fprintf(stderr, "failed to allocate connection IO\n");
    }

    quiche_conn *conn = quiche_connect(server_host, (const uint8_t *) scid, sizeof(scid),
                                       (struct sockaddr *) &local_addr,
                                       local_addr_len,
                                       (struct sockaddr *) &peer_addr, peer_addr_len,
                                       config);
    if (conn == nullptr) {
        fprintf(stderr, "failed to create connection\n");
    }
    connection->conn = conn;

    memcpy(&connection->peer_addr, &peer_addr, peer_addr_len);
    connection->peer_addr_len = peer_addr_len;

    using namespace std::chrono_literals;

    return send_data(connection, channel, server_address)
            .then([this]() {
                return seastar::keep_doing(
                        [this]() {
                            return receive();
                        });
            });

}

seastar::future<> Client::handle_connection(uint8_t *buf, ssize_t read, struct conn_io *conn_io, udp_channel &channel,
                                            udp_datagram &datagram,
                                            seastar::ipv4_addr &addr) {

    static bool echo_received = true;

    sockaddr peer_addr = datagram.get_src().as_posix_sockaddr();
    socklen_t peer_addr_len = sizeof(peer_addr);

    sockaddr local_addr = channel.local_address().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);


    if (read < 0) {

        perror("failed to read");
        return seastar::make_ready_future<>();
    }

    quiche_recv_info recv_info = {
            (struct sockaddr *) &peer_addr,
            peer_addr_len,

            (struct sockaddr *) &local_addr,
            local_addr_len,

    };

    ssize_t done = quiche_conn_recv(conn_io->conn, buf, read, &recv_info);


    if (done < 0) {
        fprintf(stderr, "failed to process packet\n");
        return seastar::make_ready_future<>();
    }

    if (quiche_conn_is_closed(conn_io->conn)) {
        fprintf(stderr, "connection closed\n");
        return handle_timeout();
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

            printf("%.*s", (int) recv_len, buf);
            echo_received = true;

            if (fin) {
                if (quiche_conn_close(conn_io->conn, true, 0, nullptr, 0) < 0) {
                    fprintf(stderr, "failed to close connection\n");
                }
            }
        }

        quiche_stream_iter_free(readable);
    }

    if (quiche_conn_is_established(conn_io->conn)) {
        const uint8_t *app_proto;
        size_t app_proto_len;

        quiche_conn_application_proto(conn_io->conn, &app_proto, &app_proto_len);


        if (echo_received) {
            // Get line from the user.
            char line_user[1024];
            fprintf(stderr, "Enter text to send: \n");
            fgets(line_user, sizeof(line_user), stdin);

            auto *line = (uint8_t *) line_user;

            if (quiche_conn_stream_send(conn_io->conn, 4, line, strlen(line_user) + 1, false) < 0) {
                return seastar::make_ready_future<>();
            }
            echo_received = false;
        }
    }

    return send_data(conn_io, channel, addr);
}

seastar::future<> Client::handle_timeout() {
    quiche_conn_on_timeout(connection->conn);
    std::cout << "getting timeout " << std::endl;
    is_timer_active = false;

    if (quiche_conn_is_closed(connection->conn)) {
        quiche_stats stats;
        quiche_path_stats path_stats;

        quiche_conn_stats(connection->conn, &stats);
        quiche_conn_path_stats(connection->conn, 0, &path_stats);

        fprintf(stderr, "connection closed, recv=%zu sent=%zu lost=%zu rtt=%" PRIu64 "ns\n",
                stats.recv, stats.sent, stats.lost, path_stats.rtt);

        exit(1);
    }

    return send_data(connection, channel, server_address);
}
seastar::future<> Client::send_data(struct conn_io *conn_data, udp_channel &chan, seastar::ipv4_addr &addr) {
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

        std::cout << "sending " << written << " bytes" << std::endl;

        (void) chan.send(addr,
                         seastar::temporary_buffer<char>(reinterpret_cast<const char *>(out), written));

    }

    std::cout << "if closed " << quiche_conn_is_closed(conn_data->conn) << std::endl;


    if (!is_timer_active) {
        auto timeout = (int64_t) quiche_conn_timeout_as_millis(conn_data->conn);


        if (timeout > 0) {
            std::cerr << "Setting the timer for " << timeout << " millis.\n";
            (void) seastar::sleep(std::chrono::milliseconds(timeout)).then([this] () {
                return handle_timeout();
            });
            is_timer_active = true;
        }
    }
    return seastar::make_ready_future<>();
}

seastar::future<> Client::receive() {
    return channel.receive().then([this](udp_datagram datagram) {

        uint8_t buffer[MAX_DATAGRAM_SIZE];
        auto fragment_array = datagram.get_data().fragment_array();
        memcpy(buffer, fragment_array->base, fragment_array->size);

        return handle_connection(buffer, fragment_array->size, connection, channel, datagram, server_address);

    });
}
