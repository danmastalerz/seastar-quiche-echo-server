#include <client/client.h>
#include <seastar/core/sleep.hh>
#include <cinttypes>
#include <fstream>

static size_t sent = 0;
static size_t read_f = 0;

void setup_config(quiche_config **config) {

    *config = quiche_config_new(0xbabababa);
    if (*config == nullptr) {
        exit(1);
    }

    quiche_config_set_application_protos(*config,
                                         (uint8_t *) "\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9", 38);

    quiche_config_set_max_idle_timeout(*config, 5000);
    quiche_config_set_max_recv_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_send_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(*config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(*config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(*config, 1000000);
    quiche_config_set_initial_max_stream_data_uni(*config, 1000000);
    quiche_config_set_initial_max_streams_bidi(*config, 100);
    quiche_config_set_initial_max_streams_uni(*config, 100);
    quiche_config_set_disable_active_migration(*config, true);
    quiche_config_set_cc_algorithm(*config, QUICHE_CC_RENO);
    quiche_config_set_max_stream_window(*config, 1000000);
    quiche_config_set_max_connection_window(*config, 1000000);

    if (getenv("SSLKEYLOGFILE")) {
        quiche_config_log_keys(*config);
    }

}

Client::Client(const char *host, uint16_t port, std::string file, int core) :
        channel(seastar::make_udp_channel()),
        server_host(host),
        server_address(seastar::ipv4_addr(host, port)),
        is_timer_active(false),
        config(nullptr),
        file(file),
        core(core),
        fin(file, std::ifstream::binary),
        send_file_buffer(std::vector<char>(FILE_CHUNK)) {}

void Client::client_setup_config() {
    setup_config(&config);
}

seastar::future<> Client::client_loop() {


    if (config == nullptr) {
        std::cerr << "Failed to setup quiche configuration...";
        return seastar::make_ready_future<>();
    }


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
        return seastar::make_ready_future<>();
    }

    sockaddr local_addr = channel.local_address().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);

    seastar::socket_address dest(server_address);

    sockaddr peer_addr = dest.as_posix_sockaddr();
    socklen_t peer_addr_len = sizeof(peer_addr);

    connection = (conn_io *) calloc(1, sizeof(conn_io));
    if (connection == nullptr) {
    }

    quiche_conn *conn = quiche_connect(server_host, (const uint8_t *) scid, sizeof(scid),
                                       (struct sockaddr *) &local_addr,
                                       local_addr_len,
                                       (struct sockaddr *) &peer_addr, peer_addr_len,
                                       config);
    if (conn == nullptr) {
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
        return seastar::make_ready_future<>();
    }

    if (quiche_conn_is_established(conn_io->conn)) {
        uint64_t s = 0;

        quiche_stream_iter *readable = quiche_conn_readable(conn_io->conn);

        while (quiche_stream_iter_next(readable, &s)) {

            bool fin = false;
            ssize_t recv_len = quiche_conn_stream_recv(conn_io->conn, s,
                                                       buf, sizeof(buf),
                                                       &fin);
            if (recv_len < 0) {
                break;
            }
        }

        quiche_stream_iter_free(readable);
    }

    if (quiche_conn_is_established(conn_io->conn)) {
        const uint8_t *app_proto;
        size_t app_proto_len;

        quiche_conn_application_proto(conn_io->conn, &app_proto, &app_proto_len);

        int streams = 1;
        int used_streams = 10;
        for (int i = 0; i < streams; i++) {
            auto x = quiche_conn_stream_send(conn_io->conn, 4*i,
                                             reinterpret_cast<const uint8_t *>(send_file_buffer.data()),
                                             FILE_CHUNK, false);
            if (x < 0) {
                fprintf(stderr, "failed to send data in stream, err=%zd", x);
                x = 0;
                used_streams--;
            }
            sent += x;
            read_f += FILE_CHUNK;
        }


        std::cout << "\033[2J\033[1;1H";
        std::cout << "Used streams: " << used_streams << std::endl;
        std::cout << "Shard: " << seastar::this_shard_id() << std::endl;
        std::cout << "Read " << read_f / 1024.0 << " kilobytes (" << read_f / 1024.0 / 1024.0 << " megabytes) in total."
                  << std::endl;
        std::cout << "Sent " << sent / 1024.0 << " kilobytes (" << sent / 1024.0 / 1024.0 << " megabytes) in total."
                  << std::endl;


    }

    return send_data(conn_io, channel, addr);
}

seastar::future<> Client::handle_timeout() {
    quiche_conn_on_timeout(connection->conn);
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

    seastar::future<> f = seastar::make_ready_future<>();


    while (true) {
        ssize_t written = quiche_conn_send(conn_data->conn, out, sizeof(out),
                                           &send_info);
        std::cout << written << std::endl;

        if (written == QUICHE_ERR_DONE) {
            break;
        }

        if (written < 0) {
            exit(1);
        }


        std::unique_ptr<char> p(new char[written]);
        std::memcpy(p.get(), out, written);

        auto timespec = send_info.at;
        struct timespec diff{};
        struct timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);

        // Calculate difference between timespec and now
        diff.tv_sec = timespec.tv_sec - now.tv_sec;
        diff.tv_nsec = timespec.tv_nsec - now.tv_nsec;

        // Convert to chrono
        auto sleep_duration = std::chrono::seconds(diff.tv_sec) + std::chrono::nanoseconds(diff.tv_nsec);

        // If negative, don't wait
        if (sleep_duration.count() < 0) {
            sleep_duration = std::chrono::seconds(0);
        }

        // Wait for f to complete before sending the next packet.
        f = f.then([this, &chan, &addr, p = std::move(p), written, &sleep_duration] () mutable {
            return seastar::sleep(sleep_duration).then([this, &chan, &addr, p = std::move(p), written] {
                return chan.send(addr,
                                 seastar::temporary_buffer<char>(p.get(), written));
            });
        });
    }


    if (!is_timer_active) {
        auto timeout = (int64_t) quiche_conn_timeout_as_millis(conn_data->conn);


        if (timeout > 0) {
            (void) seastar::sleep(std::chrono::milliseconds(timeout)).then([this]() {
                return handle_timeout();
            });
            is_timer_active = true;
        }
    }
    return f;
}

seastar::future<> Client::receive() {
    return channel.receive().then([this](udp_datagram datagram) {

        uint8_t buffer[MAX_DATAGRAM_SIZE];
        auto fragment_array = datagram.get_data().fragment_array();
        memcpy(buffer, fragment_array->base, fragment_array->size);

        return handle_connection(buffer, fragment_array->size, connection, channel, datagram, server_address);

    });
}

