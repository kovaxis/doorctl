#include "tls.hpp"
#include "util.hpp"

#include "EEPROM.h"
#include "WiFi.h"
#include <stdint.h>
#include <string.h>
#include <string>

#include "cert/cert.h"
#include "cert/private_key.h"

const int CONF_ADDRESS = 0;
const int OUT_PIN_COUNT = 2;
const int IN_PIN_COUNT = 2;
const int SECRET_SIZE = 32;

const u32 CMD_OPEN_DOOR = 1;
const u32 CMD_READ_VALUES = 2;
const u32 CMD_SET_CONF = 3;
const u32 CMD_GET_CONF = 4;

IPAddress build_ip(u8 *ip) {
    return IPAddress(ip[0], ip[1], ip[2], ip[3]);
}

void show_sockaddr(sockaddr_in &addr) {
    u8 *ip = (u8 *)&addr.sin_addr;
    Serial.print(ip[0]);
    Serial.print('.');
    Serial.print(ip[1]);
    Serial.print('.');
    Serial.print(ip[2]);
    Serial.print('.');
    Serial.print(ip[3]);
    Serial.print(':');
    Serial.print(addr.sin_port);
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
    u8 auth_token[SECRET_SIZE];
    int out_pins[OUT_PIN_COUNT];
    int in_pins[IN_PIN_COUNT];
    int repeat_pins[IN_PIN_COUNT];
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
    SHOW_BYTESTRING(auth_token);
    SHOW_INTS(out_pins);
    SHOW_INTS(in_pins);
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

void update_conf_from_serial() {
    Serial.println("enter conf hexstring:");
    read_bytestring((u8 *)&CONF, sizeof(Conf));
}

void save_conf_to_eeprom() {
    for (int i = 0; i < sizeof(Conf); i++) {
        EEPROM.write(CONF_ADDRESS + i, *(((u8 *)&CONF) + i));
    }
    EEPROM.commit();
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

void process_command(u32 cmd, TlsClient *data) {
    if (cmd == CMD_OPEN_DOOR) {
        // Set a pin to low for a while
        u8 door_idx;
        data->read_exact(&door_idx, 1);
        u16 open_time;
        data->read_exact((u8 *)&open_time, 2);
        Serial.print("  opening door ");
        Serial.print(door_idx);
        Serial.print(" for ");
        Serial.print(open_time);
        Serial.println("ms");
        if (door_idx < OUT_PIN_COUNT) {
            digitalWrite(CONF.out_pins[cmd], LOW);
            delay(open_time);
            digitalWrite(CONF.out_pins[cmd], HIGH);
        }
    } else if (cmd == CMD_READ_VALUES) {
        // Read input pin state
        Serial.println("  reading out pin levels");
        data->write_all((const u8 *)&OUT_PIN_COUNT, sizeof(OUT_PIN_COUNT));
        for (int i = 0; i < IN_PIN_COUNT; i++) {
            int value = analogRead(CONF.in_pins[i]);
            data->write_all((const u8 *)&value, sizeof(value));
        }
    } else if (cmd == CMD_SET_CONF) {
        // Update conf struct
        Serial.println("  updating conf");
        int conf_size = sizeof(Conf);
        data->write_all((const u8 *)&conf_size, sizeof(int));
        Conf tmp;
        if (data->read_exact((u8 *)&tmp, sizeof(Conf))) {
            memcpy(&CONF, &tmp, sizeof(Conf));
            Serial.println("  successfully updated conf");
            save_conf_to_eeprom();
            Serial.println("  updated conf in EEPROM");
        }
    } else if (cmd == CMD_GET_CONF) {
        // Read conf struct
        Serial.println("  reading conf");
        int conf_size = sizeof(Conf);
        data->write_all((const u8 *)&conf_size, sizeof(int));
        data->write_all((const u8 *)&CONF, sizeof(Conf));
    } else {
        Serial.println("  unknown command");
    }
}

void setup() {
    EEPROM.begin(sizeof(Conf));
    Serial.begin(115200);

    Serial.println("to update config, type 'C' now:");
    Serial.flush();
    delay(1500);
    if (Serial.available() && Serial.read() == 'C') {
        while (Serial.available()) Serial.read();
        update_conf_from_serial();
        save_conf_to_eeprom();
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

    Serial.println("starting server...");
    TlsCert cert(example_key_DER, example_key_DER_len, example_crt_DER, example_crt_DER_len);
    TlsServer server(cert, CONF.server_port);
    Serial.println("server started");

    while (true) {
        Serial.println("accepting clients...");
        TlsClient client = server.accept_client();

        if (!client.connected) continue;

        Serial.print("connection from client ");
        show_sockaddr(client.sock_addr);
        Serial.println();

        u8 token[SECRET_SIZE];
        if (!client.read_exact(token, SECRET_SIZE)) {
            client.write_str("token not supplied");
            continue;
        }

        Serial.print("client supplied token ");
        show_bytestring(token);
        Serial.println();

        if (memcmp(token, CONF.auth_token, SECRET_SIZE) != 0) {
            client.write_str("unauthorized token");
            continue;
        }

        u32 cmd;
        while (client.read_exact((u8 *)&cmd, 4)) {
            Serial.print("processing command ");
            Serial.println(cmd);
            process_command(cmd, &client);
        }
    }
}

void loop() {
}
