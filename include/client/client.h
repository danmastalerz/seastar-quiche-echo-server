#ifndef SEASTAR_QUICHE_ECHO_SERVER_CLIENT_H
#define SEASTAR_QUICHE_ECHO_SERVER_CLIENT_H


#include <quiche.h>
#include <quiche_utils.h>

#include <seastar/core/reactor.hh>
#include <seastar/core/timer.hh>

using namespace seastar::net;

class input{
private:
    std::queue<std::pair<std::vector<char>, size_t>> msgs;
public:
    explicit input() = default;
    seastar::future<> push_input(const char* msg, size_t written){
        std::vector<char> tmp(msg, msg + written);
        auto t_pair = std::make_pair(tmp, written);
        msgs.emplace(t_pair);
        return seastar::make_ready_future<>();
    };
    seastar::temporary_buffer<char> send_input(){
        auto front = msgs.front();
        msgs.pop();
        std::string input(front.first.begin(), front.first.end());
        return {input.c_str(), front.second};

    };
    size_t size(){
        return msgs.size();
    }
    bool empty(){
        return msgs.empty();
    }
};

class output{
private:
    std::queue<std::pair<std::vector<char>, size_t>> msgs;
public:
    explicit output() = default;
    seastar::future<> push_output(const char* msg, size_t written){
        std::vector<char> tmp(msg, msg + written);
        auto t_pair = std::make_pair(tmp, written);
        msgs.emplace(t_pair);
        return seastar::make_ready_future<>();
    };
    std::pair<std::string, size_t> read_output(){
        auto front = msgs.front();
        msgs.pop();
        std::string input(front.first.begin(), front.first.end());
        return {input.c_str(), front.second};
    };
    size_t size(){
        return msgs.size();
    }
    bool empty(){
        return msgs.empty();
    }
};


class Client {
private:
    udp_channel channel;
    conn_io *connection{};
    quiche_config *config;
    const char *server_host;
    seastar::ipv4_addr server_address;
    seastar::timer<> timer;
    bool is_timer_active;
    input input_buf;
    output output_buf;


    void handle_timeout();
    seastar::future<> timer_expired();

    seastar::future<> send_data(struct conn_io *conn_data, udp_channel &chan, seastar::ipv4_addr &addr);

    seastar::future<>
    handle_connection(uint8_t *buf, ssize_t read, struct conn_io *conn_io, udp_channel &channel, udp_datagram &datagram,
                      seastar::ipv4_addr &addr);

    seastar::future<> receive();


public:
    explicit Client(const char *host, std::uint16_t port);

    void client_setup_config();

    seastar::future<> client_loop();

    seastar::future<> write_input(const std::string& msg, size_t written);

    seastar::future<seastar::temporary_buffer<char>> read_output();

    seastar::future<> initialize();
};

#endif //SEASTAR_QUICHE_ECHO_SERVER_CLIENT_H
