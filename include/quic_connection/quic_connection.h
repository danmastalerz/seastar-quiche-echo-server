#ifndef SEASTAR_QUICHE_ECHO_DEMO_QUICHECONNECTION_H
#define SEASTAR_QUICHE_ECHO_DEMO_QUICHECONNECTION_H

#include <quiche.h>
#include <cstring>
#include <vector>
#include <map>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <seastar/core/reactor.hh>
#include <quiche_utils.h>
#include <fstream>

#define LOCAL_CONN_ID_LEN 16

using namespace seastar::net;

class QuicConnection;

using quic_connection_ptr = std::shared_ptr<QuicConnection>;

class QuicConnection {
private:
    std::vector<uint8_t> cid;
    quiche_conn *conn;
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len;
    seastar::future<> &udp_send_queue;
    udp_channel &channel;
    bool is_timer_active;
    std::ofstream save_stream;
    size_t received_bytes;
    size_t received_bytes_now;
    //timer
    seastar::timer<> timer;


public:
    // Unfortunately the constructor cannot be made public, because of std::make_shared requirements.
    QuicConnection(std::vector<uint8_t> &&_cid, quiche_conn *_conn,
                   sockaddr_storage _peer_addr, socklen_t _peer_addr_len,
                   seastar::future<> &_udp_send_queue, udp_channel &_channel);

    ~QuicConnection();


    // static factory
    static std::optional<quic_connection_ptr> from(quic_header_info *info, sockaddr *local_addr,
                                                    socklen_t local_addr_len, sockaddr_storage *peer_addr,
                                                    socklen_t peer_addr_len, quiche_config *config,
                                                    seastar::future<> &server_udp_send_queue,
                                                    udp_channel &server_udp_channel);

    const std::vector<uint8_t> &get_connection_id();
    bool is_closed();

    seastar::future<> receive_packet(uint8_t *receive_buffer, size_t receive_len, udp_datagram &datagram);
    seastar::future<> read_from_stream_and_append_to_file();
    seastar::future<> send_packets_out();
    seastar::future<> handle_timeout();

};

#endif //SEASTAR_QUICHE_ECHO_DEMO_QUICHECONNECTION_H
