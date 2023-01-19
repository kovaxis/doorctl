
#include "tls.hpp"

TlsServer::TlsServer(TlsCert cert, int port, in_addr_t bind_addr, int max_conns) : bind_addr(bind_addr), port(port), max_conns(max_conns) {
    // Initialize socket

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        Serial.println("unable to create socket");
        sock = -1;
    }

    int timeout = 5;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(timeout)) < 0) {
        Serial.println("setsockopt (timeout) failed");
    }

    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_addr.s_addr = bind_addr;

    if (bind(sock, (sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
        Serial.println("error binding socket");
        while (true) {}
    }

    if (listen(sock, max_conns) < 0) {
        Serial.println("error listening on socket");
        while (true) {}
    }

    // Initialize TLS connection on top of socket

    sslctx = SSL_CTX_new(TLSv1_2_server_method());
    if (sslctx == NULL) {
        Serial.println("error creating ssl context");
        while (true) {}
    }

    SSL_CTX_set_timeout(sslctx, 180);

    // Initialize certificates

    if (!SSL_CTX_use_certificate_ASN1(sslctx, cert.cert_len, cert.cert_data)) {
        Serial.println("failed to set certificate");
        while (true) {}
    }
    if (!SSL_CTX_use_RSAPrivateKey_ASN1(sslctx, cert.pk_data, cert.pk_len)) {
        Serial.println("failed to set private key");
        while (true) {}
    }
}

TlsServer::~TlsServer() {
    if (sock >= 0) close(sock), sock = -1;
    if (sslctx) SSL_CTX_free(sslctx), sslctx = NULL;
}

TlsClient TlsServer::accept_client() {
    TlsClient client;

    // Accept a TCP connection
    unsigned int addr_len = sizeof(client.sock_addr);
    client.sock = accept(sock, (sockaddr *)&client.sock_addr, &addr_len);
    if (client.sock < 0) {
        Serial.println("error accepting client");
        return client;
    }

    // Wrap it in TLS
    client.ssl = SSL_new(sslctx);
    if (!client.ssl) {
        Serial.println("SSL_new failed");
        return client;
    }

    SSL_set_fd(client.ssl, client.sock);

    int res = SSL_accept(client.ssl);
    if (res <= 0) {
        Serial.print("invalid tls handshake from client: ");
        Serial.print(res);
        Serial.print(" (with reason ");
        Serial.print(SSL_get_error(client.ssl, res));
        Serial.println(")");
        return client;
    }

    client.connected = true;
    return client;
}

TlsClient::TlsClient() {}

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
        connected = false;
        if (len < 0) {
            Serial.println("TLS error reading socket");
        }
        return 0;
    } else {
        return len;
    }
}

bool TlsClient::write(const u8 *buf, size_t len) {
    if (SSL_write(ssl, buf, len) <= 0) {
        connected = false;
        return true;
    }
}

bool TlsClient::read_exact(u8 *buf, size_t len) {
    while (len > 0) {
        size_t partial = read(buf, len);
        if (partial == 0) break;
        buf += partial;
        len -= partial;
    }
    return len == 0;
}

bool TlsClient::write_all(const u8 *buf, size_t len) {
    while (len > 0) {
        size_t partial = write(buf, len);
        if (partial == 0) break;
        buf += partial;
        len -= partial;
    }
    return len == 0;
}

bool TlsClient::write_str(const char *str) {
    size_t len = strlen(str);
    return write_all((const u8 *)str, len);
}