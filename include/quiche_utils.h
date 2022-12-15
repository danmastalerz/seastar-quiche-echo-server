#ifndef SEASTAR_QUICHE_UTILS_H
#define SEASTAR_QUICHE_UTILS_H

#include <quiche.h>
#include <cstring>
#include <vector>
#include <map>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>

#define LOCAL_CONN_ID_LEN 16

#define MAX_DATAGRAM_SIZE 65000
#define FILE_CHUNK 16000

#define MAX_TOKEN_LEN \
    sizeof("quiche") - 1 + \
    sizeof(struct sockaddr_storage) + \
    QUICHE_MAX_CONN_ID_LEN


struct quic_header_info {
    uint8_t type{};
    uint32_t version{};

    uint8_t scid[QUICHE_MAX_CONN_ID_LEN]{};
    size_t scid_len = sizeof(scid);

    uint8_t dcid[QUICHE_MAX_CONN_ID_LEN]{};
    size_t dcid_len = sizeof(dcid);

    uint8_t odcid[QUICHE_MAX_CONN_ID_LEN]{};
    size_t odcid_len = sizeof(odcid);

    uint8_t token[MAX_TOKEN_LEN]{};
    size_t token_len = sizeof(token);
};


// Will be removed once client is refactored.
struct conn_io {

    uint8_t cid[LOCAL_CONN_ID_LEN];
    quiche_conn *conn;
    struct sockaddr_storage peer_addr;
    socklen_t peer_addr_len;
};


inline void setup_config(quiche_config **config, const std::string &cert, const std::string &key) {
    *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (*config == nullptr) {
        fprintf(stderr, "failed to create config\n");
        exit(1);
    }

    quiche_config_load_cert_chain_from_pem_file(*config, "./cert.crt");
    quiche_config_load_priv_key_from_pem_file(*config, "./cert.key");

    quiche_config_set_application_protos(*config,
                                         (uint8_t *) "\x0ahq-interop\x05hq-29\x05hq-28\x05hq-27\x08http/0.9", 38);

    quiche_config_set_max_idle_timeout(*config, 5000);
    quiche_config_set_max_recv_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_send_udp_payload_size(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_initial_max_data(*config, 10000000);
    quiche_config_set_initial_max_stream_data_bidi_local(*config, 1000000);
    quiche_config_set_initial_max_stream_data_bidi_remote(*config, 1000000);
    quiche_config_set_initial_max_stream_data_uni(*config, 1000000);
    quiche_config_set_initial_max_streams_bidi(*config, 1000);
    quiche_config_set_initial_max_streams_uni(*config, 1000);
    quiche_config_set_disable_active_migration(*config, true);
    quiche_config_set_cc_algorithm(*config, QUICHE_CC_RENO);
    quiche_config_set_max_stream_window(*config, MAX_DATAGRAM_SIZE);
    quiche_config_set_max_connection_window(*config, MAX_DATAGRAM_SIZE);

    if (*config == nullptr) {
        std::cerr << "Failed to create quiche confiassag" << std::endl;
    }
}


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
        return nullptr;
    }

    ssize_t rand_len = read(rng, cid, cid_len);
    if (rand_len < 0) {
        perror("failed to create connection ID");
        return nullptr;
    }

    return cid;
}


#endif //SEASTAR_QUICHE_UTILS_H
