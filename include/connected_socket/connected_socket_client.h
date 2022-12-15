//
// Created by danielmastalerz on 14.12.22.
//

#ifndef SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_SOCKET_CLIENT_H
#define SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_SOCKET_CLIENT_H

#include <quiche.h>


#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>

using namespace seastar::net;


class connected_socket_client {
private:
    udp_channel channel;
    seastar::ipv4_addr server_address;
    quiche_config *config;
    conn_io *connection;
    std::map<int, std::vector<uint8_t>> send_buffers;
    std::map<int, std::vector<uint8_t>> receive_buffers;
    std::vector<uint8_t> global_buffer;
    seastar::future<> udp_send_queue;
    bool is_timer_active;
    void initialize_connection();
    seastar::future<> send_data();
    seastar::future<> handle_timeout();
    seastar::future<> pre_handshake_handler(udp_datagram& datagram, std::vector<uint8_t> buf, ssize_t read);
    seastar::future<> post_handshake_handler();
    bool is_connection_established();

public:
    explicit connected_socket_client(seastar::ipv4_addr server_address);
    seastar::future<> connect();
    seastar::future<> write_to_output_stream(std::vector<uint8_t>& data);
    seastar::future<std::vector<uint8_t>> read_from_output_stream();
    void setup_config();
};

connected_socket_client::connected_socket_client(seastar::ipv4_addr server_address) :
server_address(server_address),
send_buffers(),
receive_buffers(),
global_buffer(MAX_DATAGRAM_SIZE),
udp_send_queue(seastar::make_ready_future()),
is_timer_active(false),
config(nullptr),
channel(seastar::make_udp_channel()) {
}

void connected_socket_client::setup_config() {
    config = quiche_config_new(0xbabababa);
    if (config == nullptr) {
        throw std::runtime_error("Failed to create quiche config");
    }

    quiche_config_set_application_protos(config,
                                         (uint8_t *) "\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9", 38);

    quiche_config_set_max_idle_timeout(config, 5000);
    quiche_config_set_max_recv_udp_payload_size(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_send_udp_payload_size(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 1000000);
    quiche_config_set_initial_max_stream_data_uni(config, 1000000);
    quiche_config_set_initial_max_streams_bidi(config, 1000);
    quiche_config_set_initial_max_streams_uni(config, 1000);
    quiche_config_set_disable_active_migration(config, true);
    quiche_config_set_cc_algorithm(config, QUICHE_CC_RENO);
    quiche_config_set_max_stream_window(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_connection_window(config, MAX_DATAGRAM_SIZE);

    if (getenv("SSLKEYLOGFILE")) {
        quiche_config_log_keys(config);
    }
}

seastar::future<> connected_socket_client::connect() {
    initialize_connection();
    std::cout << "debug3\n";
    return seastar::repeat([this] {
        std::cout << "debug4\n";
        return send_data().then([this] {
            std::cout << "debug5\n";
            return channel.receive().then([this](udp_datagram datagram) {
                std::cout << "received\n";
                uint8_t buffer[MAX_DATAGRAM_SIZE];
                auto fragment_array = datagram.get_data().fragment_array();
                memcpy(buffer, fragment_array->base, fragment_array->size);
                return pre_handshake_handler(datagram, std::vector<uint8_t>(buffer, buffer + fragment_array->size), fragment_array->size).then([this] {
                    if (is_connection_established()) {
                        return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
                    } else {
                        return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
                    }
                });
            });
        });
    });
}

void connected_socket_client::initialize_connection() {
    uint8_t scid[LOCAL_CONN_ID_LEN];
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        throw std::runtime_error("Failed to open /dev/urandom");
    }

    ssize_t rand_len = read(rng, &scid, sizeof(scid));
    if (rand_len < 0) {
        throw std::runtime_error("Failed to read random bytes");
    }

    struct conn_io *conn_data = nullptr;
    conn_data = (conn_io *) calloc(1, sizeof(conn_io));

    if (conn_data == nullptr) {
        throw std::runtime_error("Failed to allocate connection data");
    }

    sockaddr local_addr = channel.local_address().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);

    seastar::socket_address dest(server_address);

    sockaddr peer_addr = dest.as_posix_sockaddr();
    socklen_t peer_addr_len = sizeof(peer_addr);

    connection = (conn_io *) calloc(1, sizeof(conn_io));
    if (connection == nullptr) {
    }

    quiche_conn *conn = quiche_connect("server_name", (const uint8_t *) scid, sizeof(scid),
                                       (struct sockaddr *) &local_addr,
                                       local_addr_len,
                                       (struct sockaddr *) &peer_addr, peer_addr_len,
                                       config);
    connection->conn = conn;

    memcpy(&connection->peer_addr, &peer_addr, peer_addr_len);
    connection->peer_addr_len = peer_addr_len;

}

seastar::future<> connected_socket_client::send_data() {
    uint8_t out[MAX_DATAGRAM_SIZE];

    quiche_send_info send_info;

    while (true) {
        ssize_t written = quiche_conn_send(connection->conn, out, sizeof(out),
                                           &send_info);
        std::cout << "Trying to send: " << written << std::endl;

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
        udp_send_queue = udp_send_queue.then([this, p = std::move(p), written, sleep_duration] () mutable {
            return seastar::sleep(sleep_duration).then([this, p = std::move(p), written] {
                return channel.send(server_address, seastar::temporary_buffer<char>(p.get(), written));
            });
        });
    }


    if (!is_timer_active) {
        auto timeout = (int64_t) quiche_conn_timeout_as_millis(connection->conn);


        if (timeout > 0) {
            (void) seastar::sleep(std::chrono::milliseconds(timeout)).then([this]() {
                return handle_timeout();
            });
            is_timer_active = true;
        }
    }
    return seastar::make_ready_future<>();
}

seastar::future<> connected_socket_client::handle_timeout() {
    quiche_conn_on_timeout(connection->conn);
    is_timer_active = false;
    if (quiche_conn_is_closed(connection->conn)) {
        quiche_stats stats;
        quiche_path_stats path_stats;
        quiche_conn_stats(connection->conn, &stats);
        quiche_conn_path_stats(connection->conn, 0, &path_stats);
        exit(1);
    }
    return send_data();
}

seastar::future<> connected_socket_client::pre_handshake_handler(udp_datagram& datagram, std::vector<uint8_t> buf, ssize_t read) {
    sockaddr peer_addr = datagram.get_src().as_posix_sockaddr();
    socklen_t peer_addr_len = sizeof(peer_addr);

    sockaddr local_addr = channel.local_address().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);


    if (read < 0) {
        throw std::runtime_error("Failed to from the udp channel");
    }

    quiche_recv_info recv_info = {
            (struct sockaddr *) &peer_addr,
            peer_addr_len,

            (struct sockaddr *) &local_addr,
            local_addr_len,

    };

    ssize_t done = quiche_conn_recv(connection->conn, buf.data(), read, &recv_info);


    if (done < 0) {
        return seastar::make_ready_future<>();
    }

    return send_data();
}

bool connected_socket_client::is_connection_established() {
    if (connection == nullptr || connection->conn == nullptr) {
        return false;
    }
    return quiche_conn_is_established(connection->conn);
}

#endif //SEASTAR_QUICHE_ECHO_DEMO_CONNECTED_SOCKET_CLIENT_H
