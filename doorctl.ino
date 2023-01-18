
#include "EEPROM.h"
#include "WiFi.h"
#include "esp_system.h"
#include "mbedtls/md.h"
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;

const int RECV_BUF_LEN = 128;
// The size of a hash digest
const int SECRET_SIZE = 256 / 8;
const int CONF_ADDRESS = 0;
const int OUT_PIN_COUNT = 2;
const int IN_PIN_COUNT = 2;

IPAddress build_ip(u8 *ip) {
    return IPAddress(ip[0], ip[1], ip[2], ip[3]);
}

void show_bytestring(const u8 *str) {
    for (int i = 0; i < SECRET_SIZE; i++) {
        u8 tmp = str[i];
        for (int j = 0; j < 2; j++) {
            u8 val = tmp >> 4;
            tmp <<= 4;
            if (val < 10) Serial.print((char)((u8)'0' + val));
            else Serial.print((char)((u8)'A' + val - 10));
        }
    }
}

void show_ints(int *ints, int len) {
    for (int i = 0; i < len; i++) {
        Serial.print(" ");
        Serial.print(ints[i]);
    }
}

#define SHOW_STRING(field)       \
    Serial.print(#field ": \""); \
    Serial.print(CONF.field);    \
    Serial.println("\"");

#define SHOW_IP(field)         \
    Serial.print(#field ": "); \
    Serial.println(build_ip(CONF.field));

#define SHOW_BYTESTRING(field)   \
    Serial.print(#field ": ");   \
    show_bytestring(CONF.field); \
    Serial.println();

#define SHOW_INT(field)        \
    Serial.print(#field ": "); \
    Serial.println(CONF.field);

#define SHOW_INTS(field)                                     \
    Serial.print(#field ":");                                \
    show_ints(CONF.field, sizeof(CONF.field) / sizeof(int)); \
    Serial.println();

struct __attribute__((__packed__)) Conf {
    char ap_ssid[MAX_SSID_LEN];
    char ap_password[MAX_SSID_LEN];
    u8 local_ip[4];
    u8 gateway_ip[4];
    u8 subnet_ip[4];
    int server_port;
    u8 rng_state[SECRET_SIZE];
    u8 auth_secret[SECRET_SIZE];
    int out_pins[OUT_PIN_COUNT];
    int in_pins[IN_PIN_COUNT];
    int open_duration;
    int on_threshold;
};

static Conf CONF;

void show_conf() {
    Serial.print(sizeof(Conf));
    Serial.println(" bytes of conf:");
    SHOW_STRING(ap_ssid);
    SHOW_STRING(ap_password);
    SHOW_IP(local_ip);
    SHOW_IP(gateway_ip);
    SHOW_IP(subnet_ip);
    SHOW_INT(server_port);
    SHOW_BYTESTRING(rng_state);
    SHOW_BYTESTRING(auth_secret);
    SHOW_INTS(out_pins);
    SHOW_INTS(in_pins);
    SHOW_INT(open_duration);
    SHOW_INT(on_threshold);
}

char read_char() {
    while (!Serial.available()) {}
    return Serial.read();
}

void read_bytestring(u8 *str, int len) {
    for (int i = 0; i < len; i++) {
        u8 b = 0;
        for (int j = 0; j < 2;) {
            char c = read_char();
            u8 base;
            if ('0' <= c && c <= '9') base = '0';
            else if ('a' <= c && c <= 'f') base = (u8)'a' - 10;
            else if ('A' <= c && c <= 'F') base = (u8)'A' - 10;
            else continue;
            b = (b << 4) | ((u8)c - base);
            j++;
        }
        str[i] = b;
    }
    while (read_char() != '\n') {}
}

void update_conf() {
    Serial.println("enter conf hexstring:");
    read_bytestring((u8 *)&CONF, sizeof(Conf));
}

void setupWifi() {
    // Connect to WiFi AP
    WiFi.mode(WIFI_STA);
    // if (!WiFi.config(build_ip(CONF.local_ip), build_ip(CONF.gateway_ip), build_ip(CONF.subnet_ip))) {
    //     Serial.println("failed to configure WiFi STA");
    // }
    WiFi.begin(CONF.ap_ssid, CONF.ap_password);
    Serial.print("connecting to AP \"");
    Serial.print(CONF.ap_ssid);
    Serial.print("\" ..");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(500);
    }
    Serial.println();
    Serial.print("connected to wifi with IP ");
    Serial.println(WiFi.localIP());
}

mbedtls_md_context_t hash_md_context;
mbedtls_md_type_t hash_md_type = MBEDTLS_MD_SHA256;
u8 hash_result[32];
void hash_begin() {
    mbedtls_md_init(&hash_md_context);
    mbedtls_md_setup(&hash_md_context, mbedtls_md_info_from_type(hash_md_type), 0);
    mbedtls_md_starts(&hash_md_context);
}
void hash_ingest(const unsigned char *payload, size_t len) {
    mbedtls_md_update(&hash_md_context, payload, len);
}
void hash_finish() {
    mbedtls_md_finish(&hash_md_context, hash_result);
    mbedtls_md_free(&hash_md_context);
}

int read_client_bytes(WiFiClient *client, u8 *buf, int expect) {
    int cur = 0;
    while (client->connected()) {
        if (client->available()) {
            int len = client->read(buf + cur, expect - cur);
            cur += len;
            if (cur == expect) break;
        }
    }
    return cur;
}

u8 process_command(u8 cmd) {
    if (cmd < OUT_PIN_COUNT) {
        // Open door
        digitalWrite(CONF.out_pins[cmd], LOW);
        delay(CONF.open_duration);
        digitalWrite(CONF.out_pins[cmd], HIGH);
    }
    // Read state
    byte out = 0;
    for (int i = 0; i < IN_PIN_COUNT; i++) {
        int value = analogRead(CONF.in_pins[i]);
        Serial.print("read value ");
        Serial.print(value);
        Serial.print(" for pin ");
        Serial.println(CONF.in_pins[i]);
        int is_on = value >= CONF.on_threshold;
        out |= is_on << i;
    }
    return out;
}

WiFiServer server;

void setup() {
    EEPROM.begin(sizeof(Conf));
    Serial.begin(115200);

    Serial.println("to update config, type 'C' now:");
    Serial.flush();
    delay(1500);
    if (Serial.available() && Serial.read() == 'C') {
        while (Serial.available()) Serial.read();
        update_conf();
        for (int i = 0; i < sizeof(Conf); i++) {
            EEPROM.write(CONF_ADDRESS + i, *(((u8 *)&CONF) + i));
        }
        EEPROM.commit();
        Serial.println("config updated");
    } else {
        for (int i = 0; i < sizeof(Conf); i++) {
            *(((u8 *)&CONF) + i) = EEPROM.read(CONF_ADDRESS + i);
        }
        Serial.println("config left as-is");
    }
    Serial.println("current config:");
    show_conf();

    for (int i = 0; i < OUT_PIN_COUNT; i++) {
        pinMode(CONF.out_pins[i], OUTPUT);
        digitalWrite(CONF.out_pins[i], HIGH);
    }
    for (int i = 0; i < IN_PIN_COUNT; i++) {
        pinMode(CONF.in_pins[i], INPUT);
    }

    setupWifi();
    server.begin(CONF.server_port);
    while (true) {
        Serial.println("listening for clients...");
        WiFiClient client;
        while (!client) client = server.available();

        Serial.print("connected to client ");
        Serial.print(client.remoteIP());
        Serial.print(":");
        Serial.println(client.remotePort());

        // Generate challenge
        {
            hash_begin();
            u32 entropy1 = esp_random();
            unsigned long entropy2 = micros();
            hash_ingest((u8 *)&entropy1, sizeof(entropy1));
            hash_ingest((u8 *)&entropy2, sizeof(entropy2));
            hash_ingest(CONF.rng_state, SECRET_SIZE);
            hash_finish();
            memcpy(CONF.rng_state, hash_result, SECRET_SIZE);
            Serial.print("using random entropy ");
            Serial.print(entropy1);
            Serial.print(" and ");
            Serial.println(entropy2);
        }

        // Send challenge
        client.write(CONF.rng_state, SECRET_SIZE);

        // Read message
        u8 recvbuf[SECRET_SIZE * 2];
        int recvlen = read_client_bytes(&client, recvbuf, SECRET_SIZE * 2);

        // Build the expected public key
        hash_begin();
        hash_ingest(CONF.rng_state, SECRET_SIZE);
        hash_ingest(CONF.auth_secret, SECRET_SIZE);
        hash_finish();

        // Authorize message
        bool auth = recvlen == SECRET_SIZE * 2 && memcmp(recvbuf, hash_result, SECRET_SIZE) == 0;
        if (!auth) {
            client.stop();
            Serial.println("unauthorized request");
            continue;
        }

        // Check the type of command
        u8 cmd = 255;
        for (u8 i = 0; i < 4; i++) {
            const u8 PARAM_CODE = 250;
            hash_begin();
            hash_ingest(&PARAM_CODE, 1);
            hash_ingest(&i, 1);
            hash_ingest(CONF.rng_state, SECRET_SIZE);
            hash_ingest(CONF.auth_secret, SECRET_SIZE);
            hash_finish();
            if (memcmp(recvbuf + SECRET_SIZE, hash_result, SECRET_SIZE) == 0) {
                cmd = i;
            }
        }

        // Process command
        u8 result = process_command(cmd);

        // Return result to client
        const u8 RESULT_CODE = 251;
        hash_begin();
        hash_ingest(&RESULT_CODE, 1);
        hash_ingest(&result, 1);
        hash_ingest(CONF.rng_state, SECRET_SIZE);
        hash_ingest(CONF.auth_secret, SECRET_SIZE);
        hash_finish();
        client.write(hash_result, SECRET_SIZE);

        // End connection with client
        client.stop();
    }
}

void loop() {}
