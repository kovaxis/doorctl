
#include "tls.hpp"

static bool has_data_available(int sock) {
    // We create a file descriptor set to be able to use the select function
    fd_set sockfds;
    // Out socket is the only socket in this set
    FD_ZERO(&sockfds);
    FD_SET(sock, &sockfds);

    // We define a "immediate" timeout
    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0; // Return immediately, if possible

    // Wait for input
    // As by 2017-12-14, it seems that FD_SETSIZE is defined as 0x40, but socket IDs now
    // start at 0x1000, so we need to use _socket+1 here
    select(sock + 1, &sockfds, NULL, NULL, &timeout);

    // Check if there is input
    return FD_ISSET(sock, &sockfds);
}

TlsServer::TlsServer(TlsCert cert, int port, in_addr_t bind_addr, int max_conns) : bind_addr(bind_addr), port(port), max_conns(max_conns) {
    // Initialize socket

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        Serial.println("unable to create socket");
        sock = -1;
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

bool TlsServer::try_accept(TlsClient *client) {
    // Ensure we don't block
    if (!has_data_available(sock)) return false;

    // Accept a TCP connection
    unsigned int addr_len = sizeof(client->sock_addr);
    client->sock = accept(sock, (sockaddr *)&client->sock_addr, &addr_len);
    if (client->sock < 0) {
        Serial.println("error accepting client");
        return false;
    }

    // Wrap it in TLS
    client->ssl = SSL_new(sslctx);
    if (!client->ssl) {
        Serial.println("SSL_new failed");
        return false;
    }

    SSL_set_fd(client->ssl, client->sock);

    int res = SSL_accept(client->ssl);
    if (res <= 0) {
        Serial.print("invalid tls handshake from client: ");
        Serial.print(res);
        Serial.print(" (with reason ");
        Serial.print(SSL_get_error(client->ssl, res));
        Serial.println(")");
        return false;
    }

    client->connected = true;
    return true;
}

TlsClient::TlsClient() {}

TlsClient::~TlsClient() { finish(); }

void TlsClient::finish() {
    if (ssl) {
        Serial.println("shutting down client");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        ssl = NULL;
    }
    if (sock >= 0) close(sock), sock = -1;
}

size_t TlsClient::read(u8 *buf, size_t cap) {
    if (!has_data_available(sock)) return 0;
    int len = SSL_read(ssl, buf, cap);
    if (len <= 0) {
        connected = false;
        if (len < 0) {
            Serial.print("TLS error reading socket: ");
            Serial.println(len);
        }
        return 0;
    } else {
        return len;
    }
}

size_t TlsClient::write(const u8 *buf, size_t len) {
    int partial = SSL_write(ssl, buf, len);
    if (partial <= 0) {
        connected = false;
        if (partial < 0) {
            Serial.print("TLS error writing socket: ");
            Serial.println(partial);
        }
        return 0;
    }
    return partial;
}

bool TlsClient::read_exact(u8 *buf, size_t len) {
    while (connected && len > 0) {
        size_t partial = read(buf, len);
        buf += partial;
        len -= partial;
    }
    return len == 0;
}

bool TlsClient::write_all(const u8 *buf, size_t len) {
    while (connected && len > 0) {
        size_t partial = write(buf, len);
        buf += partial;
        len -= partial;
    }
    return len == 0;
}

bool TlsClient::write_str(const char *str) {
    size_t len = strlen(str);
    return write_all((const u8 *)str, len);
}