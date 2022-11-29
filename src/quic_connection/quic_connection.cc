#include "quic_connection/quic_connection.h"
#include <cinttypes>

QuicConnection::QuicConnection(std::vector<uint8_t> &&_cid, quiche_conn *_conn,
                               sockaddr_storage _peer_addr, socklen_t _peer_addr_len) :
            cid(_cid),
            conn(_conn),
            peer_addr(_peer_addr),
            peer_addr_len(_peer_addr_len),
            udp_send_queue(seastar::make_ready_future<>()) {}


std::optional<quic_connection_ptr> QuicConnection::from(quic_header_info *info, struct sockaddr *local_addr,
                                                         socklen_t local_addr_len,
                                                         struct sockaddr_storage *peer_addr,
                                                         socklen_t peer_addr_len, quiche_config *config) {
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
    return std::make_shared<QuicConnection>(std::move(cid), conn, *peer_addr, peer_addr_len);
}


const std::vector<uint8_t>& QuicConnection::get_connection_id() {
    return cid;
}


quiche_conn* QuicConnection::get_conn() {
    return conn;
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

seastar::future<> QuicConnection::read_from_streams_and_echo() {
    uint8_t receive_buffer[MAX_DATAGRAM_SIZE];
    ssize_t receive_len;

    if (quiche_conn_is_established(conn)) {
        std::cerr << "connection is established.\n";
        uint64_t s = 0;

        quiche_stream_iter *readable = quiche_conn_readable(conn);

        while (quiche_stream_iter_next(readable, &s)) {
            fprintf(stderr, "stream %" PRIu64 " is readable\n", s);

            bool fin = false;
            receive_len = quiche_conn_stream_recv(conn, s,
                                                  receive_buffer, sizeof(receive_buffer),
                                                  &fin);

            if (receive_len < 0) {
                break;
            }

            fprintf(stderr, "Received: %s\n", receive_buffer);

            quiche_conn_stream_send(conn, s,
                                    receive_buffer, receive_len, false);

            if (fin) {
                static const char *resp = "Stream finished.\n";
                quiche_conn_stream_send(conn, s, (uint8_t *) resp,
                                        18, true);
            }
        }

        quiche_stream_iter_free(readable);
    }
    else {
        std::cerr << "connection not established yet.\n";
    }

    return seastar::make_ready_future<>();
}


seastar::future<>& QuicConnection::get_send_queue() {
    return udp_send_queue;
}