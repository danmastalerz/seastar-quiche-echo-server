//
// Created by danielmastalerz on 23.11.22.
//

#ifndef SEASTAR_QUICHE_UTILS_H
#define SEASTAR_QUICHE_UTILS_H

#include <quiche.h>

// Unix
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <cstddef>
#include <cstring>
#include <vector>
#include <map>

// include/..
#include <logger.h>

namespace zpp {

using sockaddr          = ::sockaddr;
using sockaddr_storage  = ::sockaddr_storage;
using socklen_t         = ::socklen_t;

constexpr std::size_t LOCAL_CONN_ID_LEN = 16ULL;
constexpr std::size_t MAX_DATAGRAM_SIZE = 1350ULL;
constexpr std::size_t MAX_TOKEN_LEN =
    sizeof("quiche") - 1 +
    sizeof(sockaddr_storage) +
    QUICHE_MAX_CONN_ID_LEN;

struct conn_io {
    std::uint8_t cid[LOCAL_CONN_ID_LEN];
    quiche_conn *conn;
    sockaddr_storage peer_addr;
    socklen_t peer_addr_len;
};

void setup_config(quiche_config **config, const std::string &cert, const std::string &key);

//void setup_config(quiche_config **config) {
//    setup_config(config, "./cert.crt", "./cert.key");
//}

[[maybe_unused]] static void mint_token(const std::uint8_t *dcid, std::size_t dcid_len,
                       sockaddr_storage *addr, socklen_t addr_len,
                       std::uint8_t *token, std::size_t *token_len) {
    std::memcpy(token, "quiche", sizeof("quiche") - 1);
    std::memcpy(token + sizeof("quiche") - 1, addr, addr_len);
    std::memcpy(token + sizeof("quiche") - 1 + addr_len, dcid, dcid_len);

    *token_len = sizeof("quiche") - 1 + addr_len + dcid_len;
}

[[maybe_unused]] static bool validate_token(const std::uint8_t *token, std::size_t token_len,
                           sockaddr_storage *addr, socklen_t addr_len,
                           std::uint8_t *odcid, std::size_t *odcid_len) {
    if ((token_len < sizeof("quiche") - 1) || std::memcmp(token, "quiche", sizeof("quiche") - 1)) {
        return false;
    }

    token += sizeof("quiche") - 1;
    token_len -= sizeof("quiche") - 1;

    if ((token_len < addr_len) || std::memcmp(token, addr, addr_len)) {
        return false;
    }

    token += addr_len;
    token_len -= addr_len;

    if (*odcid_len < token_len) {
        return false;
    }

    std::memcpy(odcid, token, token_len);
    *odcid_len = token_len;

    return true;
}

[[maybe_unused]] static std::uint8_t *gen_cid(std::uint8_t *cid, std::size_t cid_len) {
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        flog("Failed to open /dev/urandom");
        return nullptr;
    }

    ssize_t rand_len = read(rng, cid, cid_len);
    if (rand_len < 0) {
        flog("failed to create connection ID");
        return nullptr;
    }

    return cid;
}

[[maybe_unused]] static conn_io *create_conn(std::uint8_t *scid, std::size_t scid_len,
                            std::uint8_t *odcid, std::size_t odcid_len,
                            sockaddr *local_addr, socklen_t local_addr_len,
                            sockaddr_storage *peer_addr, socklen_t peer_addr_len,
                            quiche_config *config, std::map<std::vector<uint8_t>, conn_io*> &clients)
{
    conn_io *conn_data = nullptr;
    conn_data = (conn_io*) calloc(1, sizeof(conn_io));
    if (conn_data == nullptr) {
        flog("Failed to allocate a connection IO");
        return nullptr;
    }

    if (scid_len != LOCAL_CONN_ID_LEN) {
        flog("Failed, scid length too short");
    }

    std::memcpy(conn_data->cid, scid, LOCAL_CONN_ID_LEN);
    quiche_conn *conn = quiche_accept(scid, scid_len,
                                      odcid, odcid_len,
                                      local_addr,
                                      local_addr_len,
                                      reinterpret_cast<sockaddr*>(peer_addr),
                                      peer_addr_len,
                                      config);


    if (conn == nullptr) {
        flog("Failed to create a connection");
        return nullptr;
    }

    conn_data->conn = conn;

    std::memcpy(&conn_data->peer_addr, peer_addr, peer_addr_len);
    conn_data->peer_addr_len = peer_addr_len;

    std::vector<std::uint8_t> key(conn_data->cid, conn_data->cid + LOCAL_CONN_ID_LEN);
    clients[key] = conn_data;
    flog("New connection has been created.");

    return conn_data;
}

} // namespace zpp

#endif //SEASTAR_QUICHE_UTILS_H
