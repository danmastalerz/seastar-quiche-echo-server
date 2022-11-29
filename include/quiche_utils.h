//
// Created by danielmastalerz on 23.11.22.
//

#ifndef SEASTAR_QUICHE_UTILS_H
#define SEASTAR_QUICHE_UTILS_H

#include "quiche.h"
#include <cstring>
#include <vector>
#include <map>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>

#define LOCAL_CONN_ID_LEN 16

#define MAX_DATAGRAM_SIZE 1350

#define MAX_TOKEN_LEN \
    sizeof("quiche") - 1 + \
    sizeof(struct sockaddr_storage) + \
    QUICHE_MAX_CONN_ID_LEN

struct conn_io {

    uint8_t cid[LOCAL_CONN_ID_LEN];
    quiche_conn *conn;
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len;
};

void setup_config(quiche_config **config, const std::string &cert, const std::string &key);

//void setup_config(quiche_config **config) {
//    setup_config(config, "./cert.crt", "./cert.key");
//}

static void mint_token(const uint8_t *dcid, size_t dcid_len,
                       struct sockaddr_storage *addr, socklen_t addr_len,
                       uint8_t *token, size_t *token_len) {
    memcpy(token, "quiche", sizeof("quiche") - 1);
    memcpy(token + sizeof("quiche") - 1, addr, addr_len);
    memcpy(token + sizeof("quiche") - 1 + addr_len, dcid, dcid_len);

    *token_len = sizeof("quiche") - 1 + addr_len + dcid_len;
}

static bool validate_token(const uint8_t *token, size_t token_len,
                           struct sockaddr_storage *addr, socklen_t addr_len,
                           uint8_t *odcid, size_t *odcid_len) {
    if ((token_len < sizeof("quiche") - 1) ||
        memcmp(token, "quiche", sizeof("quiche") - 1)) {
        return false;
    }

    token += sizeof("quiche") - 1;
    token_len -= sizeof("quiche") - 1;

    if ((token_len < addr_len) || memcmp(token, addr, addr_len)) {
        return false;
    }

    token += addr_len;
    token_len -= addr_len;

    if (*odcid_len < token_len) {
        return false;
    }

    memcpy(odcid, token, token_len);
    *odcid_len = token_len;

    return true;
}

static uint8_t *gen_cid(uint8_t *cid, size_t cid_len) {
    int rng = open("/dev/urandom", O_RDONLY);
    if (rng < 0) {
        perror("failed to open /dev/urandom");
        return NULL;
    }

    ssize_t rand_len = read(rng, cid, cid_len);
    if (rand_len < 0) {
        perror("failed to create connection ID");
        return NULL;
    }

    return cid;
}

static struct conn_io *create_conn(uint8_t *scid, size_t scid_len,
                                   uint8_t *odcid, size_t odcid_len,
                                   struct sockaddr *local_addr,
                                   socklen_t local_addr_len,
                                   struct sockaddr_storage *peer_addr,
                                   socklen_t peer_addr_len, struct quiche_config* config,
                                   std::map<std::vector<uint8_t>, struct conn_io*> &clients)
{
    struct conn_io *conn_data = NULL;
    conn_data = (conn_io*) calloc(1, sizeof(conn_io));
    if (conn_data == NULL) {
        fprintf(stderr, "failed to allocate connection IO\n");
        return NULL;
    }

    if (scid_len != LOCAL_CONN_ID_LEN) {
        fprintf(stderr, "failed, scid length too short\n");
    }

    memcpy(conn_data->cid, scid, LOCAL_CONN_ID_LEN);
    quiche_conn *conn = quiche_accept(scid, scid_len,
                                      odcid, odcid_len,
                                      local_addr,
                                      local_addr_len,
                                      (struct sockaddr*) peer_addr,
                                      peer_addr_len,
                                      config);


    if (conn == NULL) {
        fprintf(stderr, "failed to create connection\n");
        return NULL;
    }

    conn_data->conn = conn;

    memcpy(&conn_data->peer_addr, peer_addr, peer_addr_len);
    conn_data->peer_addr_len = peer_addr_len;

    std::vector<uint8_t> key(conn_data->cid, conn_data->cid + LOCAL_CONN_ID_LEN);
    clients[key] = conn_data;
    fprintf(stderr, "New connection has been created.\n");

    return conn_data;
}



#endif //SEASTAR_QUICHE_UTILS_H
