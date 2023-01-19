// Based on ESP32_HTTPS_SERVER
// https://github.com/fhessel/esp32_https_server

#ifndef TLS_HPP
#define TLS_HPP

#include "lwip/inet.h"
#include "lwip/sockets.h"

#undef IPADDR_NONE
#undef INADDR_NONE

#include "openssl/ssl.h"
#include "util.hpp"
#include <Arduino.h>

struct TlsCert {
    const u8 *pk_data;
    int pk_len;
    const u8 *cert_data;
    int cert_len;

    TlsCert() : pk_data(NULL), pk_len(0), cert_data(NULL), cert_len(0) {}
    TlsCert(const u8 *pk_data, int pk_len, const u8 *cert_data, int cert_len) : pk_data(pk_data), pk_len(pk_len), cert_data(cert_data), cert_len(cert_len) {}
};

struct TlsClient {
    sockaddr_in sock_addr;
    int sock = -1;
    SSL *ssl = NULL;
    bool connected = false;

    TlsClient();
    ~TlsClient();

    size_t read(u8 *buf, size_t cap);
    bool write(const u8 *buf, size_t cap);

    bool read_exact(u8 *buf, size_t len);
    bool write_all(const u8 *buf, size_t len);
    bool write_str(const char *buf);
};

struct TlsServer {
    in_addr_t bind_addr;
    int port;
    int max_conns;

    int sock = -1;
    sockaddr_in sock_addr;
    SSL_CTX *sslctx = NULL;

    TlsServer(TlsCert cert, int port, in_addr_t bind_addr = 0, int max_conns = 1);
    ~TlsServer();
    TlsClient accept_client();
};

#endif
