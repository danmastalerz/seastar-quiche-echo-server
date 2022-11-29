#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netdb.h>

#include <quiche.h>
#include <quiche_utils.h>

#include <seastar/core/seastar.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/future-util.hh>

#include <stdexcept>
#include <seastar/core/distributed.hh>
#include "seastar/net/api.hh"
#include <iostream>
#include <seastar/core/app-template.hh>
#include <seastar/core/reactor.hh>
#include <seastar/util/log.hh>
#include <seastar/core/iostream.hh>

constexpr std::size_t LOCAL_CONN_ID_LEN = 16ULL;

constexpr char const *const host = "127.0.0.1";
constexpr std::uint16_t port = 1234;

using namespace seastar::net;
using namespace zpp;

seastar::future<> x = seastar::make_ready_future<>();

seastar::future<> send_data(conn_io &conn_data, udp_channel &chan, seastar::ipv4_addr &addr) {
    std::uint8_t out[MAX_DATAGRAM_SIZE];

    quiche_send_info send_info;

    while (true) {
        ssize_t written = quiche_conn_send(conn_data.conn, out, sizeof(out),
                                           &send_info);

        if (written == QUICHE_ERR_DONE) {
            break;
        }

        if (written < 0) {
            ffail("Failed to create a packet: ", written);
        }

        flog("Sending ", written, " bytes");

        x = chan.send(addr,
                      seastar::temporary_buffer<char>(reinterpret_cast<const char *>(out), written));

    }
    return seastar::make_ready_future<>();
}


static bool echo_received = true;


seastar::future<>
handle_connection(std::uint8_t *buf, ssize_t read, conn_io *conn_io, udp_channel &channel, udp_datagram &datagram,
                  seastar::ipv4_addr &addr) {


    sockaddr peer_addr = datagram.get_src().as_posix_sockaddr();
    socklen_t peer_addr_len = sizeof(peer_addr);

    sockaddr local_addr = channel.local_address().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);


    if (read < 0) {
        eflog("Failed to read");
        return seastar::make_ready_future<>();
    }

    quiche_recv_info recv_info = {
            reinterpret_cast<sockaddr*>(std::addressof(peer_addr)),
            peer_addr_len,
            reinterpret_cast<sockaddr*>(std::addressof(local_addr)),
            local_addr_len,

    };

    ssize_t done = quiche_conn_recv(conn_io->conn, buf, read, &recv_info);

    if (done < 0) {
        eflog("Failed to process packet");
        return seastar::make_ready_future<>();
    }


    if (quiche_conn_is_closed(conn_io->conn)) {
        eflog("Connection closed");
        return seastar::make_ready_future<>();
    }

    if (quiche_conn_is_established(conn_io->conn)) {
        uint64_t s = 0;

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

            printf("%.*s", (int) recv_len, buf);
            echo_received = true;

            if (fin) {
                if (quiche_conn_close(conn_io->conn, true, 0, nullptr, 0) < 0) {
                    eflog("Failed to close connection");
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

    return send_data(*conn_io, channel, addr);
}

seastar::future<> receive(struct conn_io &conn_io, udp_channel &channel, seastar::ipv4_addr &addr) {
    return channel.receive().then([&conn_io, &channel, &addr](udp_datagram datagram) {

        uint8_t buffer[MAX_DATAGRAM_SIZE];
        auto fragment_array = datagram.get_data().fragment_array();
        memcpy(buffer, fragment_array->base, fragment_array->size);

        return handle_connection(buffer, fragment_array->size, &conn_io, channel, datagram, addr);

    });
}

seastar::future<> client_loop() {

    std::cout << "starting client loop" << std::endl;

    quiche_config *config = quiche_config_new(0xbabababa);
    if (config == nullptr) {
        fprintf(stderr, "failed to create config\n");
        return seastar::make_ready_future<>();
    }

    quiche_config_set_application_protos(config,
                                         (uint8_t *) "\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9", 38);

    quiche_config_set_max_idle_timeout(config, 5000);
    quiche_config_set_max_recv_udp_payload_size(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_send_udp_payload_size(config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000);
    quiche_config_set_initial_max_stream_data_uni(config, 1000000);
    quiche_config_set_initial_max_streams_bidi(config, 100);
    quiche_config_set_initial_max_streams_uni(config, 100);
    quiche_config_set_disable_active_migration(config, true);

    if (getenv("SSLKEYLOGFILE")) {
        quiche_config_log_keys(config);
    }

    std::uint8_t scid[LOCAL_CONN_ID_LEN];
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        eflog("Failed to open /dev/urandom");
        return seastar::make_ready_future<>();
    }

    ssize_t rand_len = read(rng, &scid, sizeof(scid));
    if (rand_len < 0) {
        eflog("Failed to create connection ID");
        return seastar::make_ready_future<>();
    }

    conn_io *conn_data = reinterpret_cast<conn_io*>(calloc(1, sizeof(conn_io)));

    if (conn_data == nullptr) {
        eflog("Failed to allocate connection IO");
        return seastar::make_ready_future<>();
    }

    return seastar::do_with(seastar::make_udp_channel(), seastar::ipv4_addr(host, port),
                            [&config, &scid](udp_channel &channel, seastar::ipv4_addr &addr) {
                                flog("Starting do_with");
                                sockaddr local_addr = channel.local_address().as_posix_sockaddr();
                                socklen_t local_addr_len = sizeof(local_addr);

                                seastar::socket_address dest(addr);

                                sockaddr peer_addr = dest.as_posix_sockaddr();
                                socklen_t peer_addr_len = sizeof(peer_addr);

                                conn_io *conn_data = reinterpret_cast<conn_io*>(calloc(1, sizeof(conn_io)));
                                if (conn_data == nullptr) {
                                    eflog("Failed to allocate connection IO");
                                }

                                quiche_conn *conn = quiche_connect(
                                    host,
                                    reinterpret_cast<const std::uint8_t*>(scid),
                                    sizeof(scid),
                                    reinterpret_cast<sockaddr*>(std::addressof(local_addr)),
                                    local_addr_len,
                                    reinterpret_cast<sockaddr*>(std::addressof(peer_addr)),
                                    peer_addr_len,
                                    config
                                );
                                
                                if (conn == nullptr) {
                                    eflog("Failed to create connection");
                                }
                                conn_data->conn = conn;

                                std::memcpy(&conn_data->peer_addr, &peer_addr, peer_addr_len);
                                conn_data->peer_addr_len = peer_addr_len;

                                using namespace std::chrono_literals;
                                return send_data(*conn_data, channel, addr)
                                        .then([&conn_data, &channel, &dest, &addr]() {

                                            return seastar::do_with(conn_data, addr,
                                                                    [&channel](auto &conn_data, auto &addr) {

                                                                        return seastar::keep_doing(
                                                                                [&channel, &conn_data, &addr] {
                                                                                    return receive(*conn_data, channel,
                                                                                                   addr);
                                                                                });
                                                                    });
                                        });
                            });
}

seastar::future<> f() {
    return seastar::parallel_for_each(boost::irange<unsigned>(0, seastar::smp::count),
                                      [](unsigned core) {
                                          return seastar::smp::submit_to(core, client_loop);
                                      });
}

int main(int argc, char **argv) {
    seastar::app_template app;

    namespace po = boost::program_options;
    app.add_options()
            ("port", po::value<std::uint16_t>()->default_value(1234), "server port");

    try {
        app.run(argc, argv, [&]() {
            auto &&config = app.configuration();
            port = config["port"].as<std::uint16_t>();
            return f();
        });
    } catch (...) {
        ffail("Couldn't start application: ", std::current_exception());
    }
    return 0;
}
