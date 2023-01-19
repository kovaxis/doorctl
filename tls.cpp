
#include "tls.hpp"

TlsServer::TlsServer(int port, in_addr_t bind_addr, int max_conns) : bind_addr(bind_addr), port(port), max_conns(max_conns) {
    // Initialize socket

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        Serial.println("unable to create socket");
        sock = -1;
    }

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = bind_addr;

    if (bind(sock, (sockaddr *)sock_addr, sizeof(sock_addr)) < 0) {
        Serial.println("error binding socket");
        while (true) {}
    }

    if (listen(sock, max_conns) < 0) {
        Serial.println("error listening on socket");
        while (true) {}
    }

    // Initialize TLS connection on top of socket

    sslctx = SSL_CTX_new(TLSv1_3_server_method());
    if (sslctx == NULL) {
        Serial.println("error creating ssl context");
        while (true) {}
    }

    SSL_CTX_set_timeout(sslctx, 180);

    // TODO: Initialize certificates
}

TlsServer::~TlsServer() {
    if (sock >= 0) close(sock), sock = -1;
    if (sslctx) SSL_CTX_free(sslctx), sslctx = NULL;
}

TlsClient TlsServer::accept() {
    TlsClient client;

    // Accept a TCP connection
    client.sock = accept(sock, &client.sock_addr, sizeof(client.sock_addr));
    if (client.sock < 0) {
        Serial.println("error accepting client");
        return client;
    }

    // Wrap it in TLS
    client.ssl = SSL_new(sslctx);
    SSL_set_fd(client.ssl, client_sock);

    if (SSL_accept(ssl) <= 0) {
        Serial.println("invalid tls handshake from client");
        return client;
    }

    client.connected = true;
    return client;
}

TlsClient::~TlsClient() {
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (sock >= 0) close(sock);
}

size_t TlsClient::read(u8 *buf, size_t cap) {
    size_t len = SSL_read(ssl, buf, cap);
    if (len <= 0) {
        client.connected = false;
        if (len < 0) {
            Serial.println("TLS error reading socket");
        }
        return 0;
    } else {
        return len;
    }
}

bool TlsClient::write(u8 *buf, size_t len) {
    if (SSL_write(ssl, buf, len) <= 0) {
        connected = false;
        return true;
    }
}
