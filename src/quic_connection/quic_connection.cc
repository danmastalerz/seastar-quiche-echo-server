#include "quic_connection/quic_connection.h"
#include <cinttypes>
#include <seastar/core/sleep.hh>

QuicConnection::QuicConnection(std::vector<uint8_t> &&_cid, quiche_conn *_conn,
                               sockaddr_storage _peer_addr, socklen_t _peer_addr_len,
                               seastar::future<> &_udp_send_queue,
                               udp_channel &_udp_channel) :
            cid(_cid),
            conn(_conn),
            peer_addr(_peer_addr),
            peer_addr_len(_peer_addr_len),
            udp_send_queue(_udp_send_queue),
            channel(_udp_channel),
            is_timer_active(false),
            save_stream("saved_file.txt", std::ofstream::binary),
            received_bytes(0),
            received_bytes_now(0) {
}


QuicConnection::~QuicConnection() {
    quiche_conn_free(conn);
}


std::optional<quic_connection_ptr> QuicConnection::from(quic_header_info *info, struct sockaddr *local_addr,
                                                         socklen_t local_addr_len, struct sockaddr_storage *peer_addr,
                                                         socklen_t peer_addr_len, quiche_config *config,
                                                         seastar::future<> &server_send_udp_queue,
                                                         udp_channel &server_udp_channel) {
    if (info->scid_len != LOCAL_CONN_ID_LEN) {
        std::cerr << "Connection creation failed - scid length too short\n";
        return std::nullopt;
    }

    quiche_conn *conn = quiche_accept(info->dcid, info->dcid_len, info->odcid,
                                      info->odcid_len, local_addr, local_addr_len,
                                      (struct sockaddr*) peer_addr, peer_addr_len, config);


    if (conn == nullptr) {
        std::cerr << "Connection creation failed - quiche_accept.\n";
        return std::nullopt;
    }

    std::vector<uint8_t> cid(info->dcid, info->dcid + LOCAL_CONN_ID_LEN);
    return std::make_shared<QuicConnection>(std::move(cid), conn, *peer_addr, peer_addr_len,
                                            server_send_udp_queue, server_udp_channel);
}


const std::vector<uint8_t>& QuicConnection::get_connection_id() {
    return cid;
}


bool QuicConnection::is_closed() {
    return quiche_conn_is_closed(conn);
}


seastar::future<> QuicConnection::receive_packet(uint8_t *receive_buffer, size_t receive_len, udp_datagram &datagram) {

    sockaddr local_addr = datagram.get_dst().as_posix_sockaddr();
    socklen_t local_addr_len = sizeof(local_addr);

    quiche_recv_info recv_info = {
            (struct sockaddr *) &peer_addr,
            peer_addr_len,
            (struct sockaddr *) &local_addr,
            local_addr_len,
    };

    ssize_t done = quiche_conn_recv(conn, receive_buffer, receive_len, &recv_info);
    if (done < 0) {
        std::cerr << "failed to process packet: " << done << "\n";
    }

    return seastar::make_ready_future<>();
}


seastar::future<> QuicConnection::read_from_stream_and_append_to_file() {
    uint8_t receive_buffer[MAX_DATAGRAM_SIZE];
    ssize_t receive_len;

    if (quiche_conn_is_established(conn)) {
        uint64_t s = 0;

        quiche_stream_iter *readable = quiche_conn_readable(conn);

        while (quiche_stream_iter_next(readable, &s)) {

            bool fin = false;
            receive_len = quiche_conn_stream_recv(conn, s, receive_buffer, sizeof(receive_buffer), &fin);
            received_bytes += receive_len;
            received_bytes_now += receive_len;
            if (receive_len < 0) {
                break;
            }


            if (fin) {
                std::cout << "Stream " << s << " is done" << std::endl;
                save_stream.close();
                static const char *resp = "Stream finished.\n";
                quiche_conn_stream_send(conn, s, (uint8_t *) resp, strlen(resp) + 1, true);
            }
        }

        quiche_stream_iter_free(readable);
    }
    else {
        std::cerr << "connection not established yet.\n";
    }

    return seastar::make_ready_future<>();
}


seastar::future<> QuicConnection::send_packets_out() {
    return seastar::repeat([this] () {
        uint8_t out[MAX_DATAGRAM_SIZE];
        quiche_send_info send_info;

        ssize_t written = quiche_conn_send(conn, out, sizeof(out), &send_info);

        if (written == QUICHE_ERR_DONE) {
            return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::yes);
        }

        if (written < 0) {
            std::cerr << "failed to create packet: " << written << "\n";
        }

        std::unique_ptr<char> p(new char[written]);
        std::memcpy(p.get(), out, written);

        udp_send_queue = udp_send_queue.then([this, p = std::move(p), written, send_info] () {

            sockaddr_in addr_in{};
            memcpy(&addr_in, &send_info.to, send_info.to_len);
            seastar::socket_address addr(addr_in);

            return channel.send(addr, seastar::temporary_buffer<char>(p.get(), written));
        });

        return seastar::make_ready_future<seastar::stop_iteration>(seastar::stop_iteration::no);
    }).then([this] () {
        // Set the timer
        if (!is_timer_active) {
            // For some reason quiche_conn_timeout_* starts to return -1 (uint64_MAX)
            auto timeout = (int64_t) quiche_conn_timeout_as_millis(conn);
            if (timeout > 0) {
                (void) seastar::sleep(std::chrono::milliseconds(timeout)).then([this] () {
                    return handle_timeout();
                });
                is_timer_active = true;
            }
        }
        return seastar::make_ready_future<>();
    });
}


seastar::future<> QuicConnection::handle_timeout() {
    quiche_conn_on_timeout(conn);
    is_timer_active = false;
    return send_packets_out();
}
