
#ifndef TLS_HPP
#define TLS_HPP

#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "openssl/ssl.h"

struct TlsServer {
    in_addr_t bind_addr;
    int port;
    int max_conns;

    int sock = -1;
    sockaddr_in sock_addr = 0;
    SSL_CTX *sslctx = NULL;

    TlsServer(int port, in_addr_t bind_addr = 0, int max_conns = 1);
    ~TlsServer();
    TlsClient accept();
};

struct TlsClient {
    sockaddr_in sock_addr = 0;
    int sock = -1;
    SSL *ssl = NULL;
    bool connected = false;

    TlsClient();
    ~TlsClient();

    size_t read(u8 *buf, size_t cap);
    size_t write(u8 *buf, size_t cap);
};

#endif