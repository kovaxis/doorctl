#include "util.hpp"

#include "EEPROM.h"
#include "WiFi.h"
#include "WiFiUdp.h"
#include "blake3.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <stdint.h>
#include <string.h>
#include <string>

const int CONF_ADDRESS = 0;
const int OUT_PIN_COUNT = 2;
const int IN_PIN_COUNT = 2;
const int SECRET_SIZE = 32;

const u32 CMD_OPEN_DOOR = 1;
const u32 CMD_READ_VALUES = 2;
const u32 CMD_SET_CONF = 3;
const u32 CMD_GET_CONF = 4;

const u8 STATUS_ERR = 0;
const u8 STATUS_OK = 1;
const u8 STATUS_REPLAY = 2;

u8 msg_buffer[512];
WiFiUDP udp;

blake3_hasher hasher;
u64 timeline_id;
u64 last_message;

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
    u8 auth_token[SECRET_SIZE];
    int out_pins[OUT_PIN_COUNT];
    int in_pins[IN_PIN_COUNT];
    int repeat_pins[IN_PIN_COUNT];
    u8 negate_out;
    u8 negate_in;
    u8 negate_repeat;
    int in_threshold;
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
    SHOW_INTS(repeat_pins);
    SHOW_INT(negate_out);
    SHOW_INT(negate_in);
    SHOW_INT(negate_repeat);
    SHOW_INT(in_threshold);
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

void udp_reply(u8 status, const u8 *msg, size_t n, u64 expiration, u64 now) {
    // compute signature
    u8 signature[BLAKE3_OUT_LEN];
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, &timeline_id, sizeof(timeline_id));
    blake3_hasher_update(&hasher, &expiration, sizeof(expiration));
    blake3_hasher_update(&hasher, &now, sizeof(now));
    blake3_hasher_update(&hasher, &status, 1);
    if (msg) blake3_hasher_update(&hasher, msg, n);
    blake3_hasher_update(&hasher, CONF.auth_token, SECRET_SIZE);
    blake3_hasher_finalize(&hasher, signature, BLAKE3_OUT_LEN);

    if (udp.beginPacket(udp.remoteIP(), udp.remotePort())) {
        udp.write(signature, BLAKE3_OUT_LEN);
        udp.write((u8 *)&timeline_id, sizeof(timeline_id));
        udp.write((u8 *)&expiration, sizeof(expiration));
        udp.write((u8 *)&now, sizeof(now));
        udp.write(&status, 1);
        if (msg) udp.write(msg, n);
        udp.endPacket();
    }
}

bool take_udp_piece(int *size, u8 **src, void *dst, size_t n, const char *err, u64 expiration, u64 now) {
    if (*size < n) {
        udp_reply(STATUS_ERR, (const u8 *)err, strlen(err), expiration, now);
        Serial.print("message truncated, expected ");
        Serial.print(err);
        Serial.print(" (");
        Serial.print(n);
        Serial.println(" bytes)");
        return false;
    }

    memcpy(dst, *src, n);
    *size -= n;
    *src += n;

    return true;
}

void udp_reply_error(const char *reply, u64 expiration, u64 now) {
    udp_reply(STATUS_ERR, (const u8 *)reply, strlen(reply), expiration, now);
    Serial.println(reply);
}

void setupWifi() {
    // Connect to WiFi AP
    WiFi.mode(WIFI_STA);
    // The following code sets a static IP (not desirable in this case, since we can set the static IP from the router instead, and avoid setting the gateway and subnet IPs)
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

void set_out(int i, bool active) {
    if (CONF.negate_out) active = !active;
    digitalWrite(CONF.out_pins[i], active);
}

void set_rep(int i, bool active) {
    if (CONF.negate_repeat) active = !active;
    digitalWrite(CONF.repeat_pins[i], active);
}

int get_in_analog(int i) {
    return analogRead(CONF.in_pins[i]);
}

int get_in_digital(int i) {
    bool active = (get_in_analog(i) >= CONF.in_threshold);
    if (CONF.negate_in) active = !active;
    return active;
}

void process_command(int packet_size, u8 *msg, u64 expiration, u64 now) {
    u32 cmd;
    if (!take_udp_piece(&packet_size, &msg, &cmd, sizeof(cmd), "cmd", expiration, now)) {
        return;
    }

    if (cmd == CMD_OPEN_DOOR) {
        // Set a pin to low for a while
        u8 door_idx;
        if (!take_udp_piece(&packet_size, &msg, &door_idx, sizeof(door_idx), "door_idx", expiration, now)) {
            return;
        }
        u16 open_time;
        if (!take_udp_piece(&packet_size, &msg, &open_time, sizeof(open_time), "open_time", expiration, now)) {
            return;
        }
        Serial.print("  opening door ");
        Serial.print(door_idx);
        Serial.print(" for ");
        Serial.print(open_time);
        Serial.println("ms");
        if (door_idx < OUT_PIN_COUNT) {
            set_out(door_idx, true);
            delay(open_time);
            set_out(door_idx, false);
        }
        const char *message = "door opened";
        udp_reply(STATUS_OK, (const u8 *)message, strlen(message), expiration, now);
    } else if (cmd == CMD_READ_VALUES) {
        // Read input pin state
        Serial.println("  reading out pin levels");
        u8 answer[sizeof(u32) * IN_PIN_COUNT];
        for (int i = 0; i < IN_PIN_COUNT; i++) {
            u32 value = get_in_analog(i);
            memcpy(&answer[i * sizeof(u32)], &value, sizeof(u32));
        }
        udp_reply(STATUS_OK, answer, sizeof(answer), expiration, now);
    } else if (cmd == CMD_SET_CONF) {
        // Update conf struct
        Serial.println("  updating conf");
        Conf tmp;
        if (!take_udp_piece(&packet_size, &msg, &tmp, sizeof(tmp), "config", expiration, now)) {
            return;
        }
        memcpy(&CONF, &tmp, sizeof(Conf));
        Serial.println("  successfully updated conf");
        save_conf_to_eeprom();
        Serial.println("  updated conf in EEPROM");
        u32 conf_size = sizeof(Conf);
        udp_reply(STATUS_OK, (const u8 *)&conf_size, sizeof(u32), expiration, now);
        ESP.restart();
    } else if (cmd == CMD_GET_CONF) {
        // Read conf struct
        Serial.println("  reading conf");
        udp_reply(STATUS_OK, (const u8 *)&CONF, sizeof(CONF), expiration, now);
    } else {
        udp_reply_error("unknown command", expiration, now);
    }
}

void do_idle_work() {
    for (int i = 0; i < IN_PIN_COUNT; i++) {
        set_rep(i, get_in_digital(i));
    }
    delay(50);
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
        set_out(i, false);
    }
    for (int i = 0; i < IN_PIN_COUNT; i++) {
        pinMode(CONF.in_pins[i], INPUT);
    }
    for (int i = 0; i < IN_PIN_COUNT; i++) {
        pinMode(CONF.repeat_pins[i], OUTPUT);
    }
    do_idle_work();

    esp_fill_random(&timeline_id, sizeof(timeline_id));
    last_message = 0;

    setupWifi();

    udp.begin(CONF.server_port);

    Serial.print("listening for udp messages on port ");
    Serial.print(CONF.server_port);
    Serial.println("...");

    while (true) {
        int packet_size = udp.parsePacket();
        if (packet_size == 0) {
            // no message
            do_idle_work();
            continue;
        }

        Serial.print("received ");
        Serial.print(packet_size);
        Serial.print(" bytes from ");
        Serial.print(udp.remoteIP().toString().c_str());
        Serial.print(":");
        Serial.println(udp.remotePort());

        packet_size = udp.read(msg_buffer, sizeof(msg_buffer));
        u8 *msg = msg_buffer;
        u64 now = esp_timer_get_time();
        u64 expiration = 0;
        if (packet_size >= BLAKE3_OUT_LEN + sizeof(u64) + sizeof(u64)) {
            memcpy(&expiration, &msg[BLAKE3_OUT_LEN + sizeof(u64)], sizeof(u64));
        }

        // make sure that the message is properly signed
        u8 received_hash[BLAKE3_OUT_LEN];
        if (!take_udp_piece(&packet_size, &msg, received_hash, BLAKE3_OUT_LEN, "signature", expiration, now)) {
            continue;
        }
        u8 computed_hash[BLAKE3_OUT_LEN];
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, msg, packet_size);
        blake3_hasher_update(&hasher, &CONF.auth_token, SECRET_SIZE);
        blake3_hasher_finalize(&hasher, computed_hash, BLAKE3_OUT_LEN);
        if (memcmp(computed_hash, received_hash, BLAKE3_OUT_LEN) != 0) {
            udp_reply_error("invalid message signature", expiration, now);
            continue;
        }

        // check the timeline
        u64 received_timeline;
        if (!take_udp_piece(&packet_size, &msg, &received_timeline, sizeof(received_timeline), "timeline", expiration, now)) {
            continue;
        }
        if (received_timeline != timeline_id) {
            udp_reply_error("invalid timeline id", expiration, now);
            continue;
        }

        // check the current time
        if (!take_udp_piece(&packet_size, &msg, &expiration, sizeof(expiration), "expiration", expiration, now)) {
            continue;
        }
        if (now >= expiration) {
            udp_reply_error("message expired", expiration, now);
            Serial.print("message has already expired ");
            Serial.print(now - expiration);
            Serial.println(" microseconds ago");
            continue;
        }
        if (expiration <= last_message) {
            const char *replay_msg = "message already received";
            udp_reply(STATUS_REPLAY, (const u8 *)replay_msg, strlen(replay_msg), expiration, now);
            Serial.print("message is sequenced before previous message, at ");
            Serial.print(expiration);
            Serial.print(" while last message was at ");
            Serial.println(last_message);
            continue;
        }
        if (expiration - now > 5000000) {
            udp_reply_error("expiration too long", expiration, now);
            Serial.print("message expires too far into the future, in ");
            Serial.print(expiration - now);
            Serial.println(" microseconds from now");
            continue;
        }
        last_message = expiration;

        // finally, process message
        process_command(packet_size, msg, expiration, now);

        Serial.print("finished processing message ");
        Serial.print(timeline_id);
        Serial.print("-");
        Serial.println(expiration);
    }
}

void loop() {
}
