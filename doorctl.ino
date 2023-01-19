
#include "EEPROM.h"
#include "WiFi.h"
#include <stdint.h>
#include <string.h>
#include <string>

#include "tls.hpp"
#include "util.hpp"

SSLCert certificate = SSLCert(
    crt_DER, crt_DER_len,
    key_DER, key_DER_len, );

HTTPSServer secure_server = HTTPSServer(&certificate);

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

void handle_get(HTTPRequest *req, HTTPResponse *res) {
    std::string bearer = req->getHeader("Authorization");
}

void handle_post(HTTPRequest *req, HTTPResponse *res) {
}

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

    secure_server.registerNode(new ResourceNode("/", "GET", &handle_get));
    secure_server.registerNode(new ResourceNode("/", "POST", &handle_post));

    Serial.println("starting server...");
    secure_sever.start();
    while (!secure_sever.isRunning()) {}
    Serial.println("server started");
}

void loop() {
    secure_server.loop();
}
