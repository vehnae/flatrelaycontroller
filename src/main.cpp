#include <Arduino.h>
#include <EtherCard.h>
#include "indexhtml.h"
#include "config.h"

enum State
{
	OFF = 0,
	ON
};

#define ETHERNET_BUFFER_SIZE 1000
#define TRANSMIT_SIZE 950

static byte macAddress[] = { 0x32, 0x9D, 0x14, 0x66, 0x7D, 0x65 };
byte Ethernet::buffer[ETHERNET_BUFFER_SIZE];
static BufferFiller bfill;

const static uint8_t ip[] = {10,1,2,109};
const static uint8_t gw[] = {10,1,2,1};
const static uint8_t dns[] = {8,8,8,8};

static int relayPins[] = { 8, 7, 6, 5 };
static State relayState[] = { OFF, OFF, OFF, OFF };
static int flatRelayPin = 8;
static int deviceId = 19; // FLAT_MAN

int lightStatus = OFF;

void setup() {
    Serial.begin(9600);
    pinMode(flatRelayPin, OUTPUT);
    digitalWrite(flatRelayPin, LOW);

    if (ether.begin(sizeof Ethernet::buffer, macAddress, SS) == 0) {
        Serial.println("Ethernet failed");
    } else {
        ether.staticSetup(ip, gw, dns);
/*        if (!ether.dhcpSetup())
            Serial.println("DHCP failed"); */
    }
}

void setRelay(int relay, State state) {
    digitalWrite(relayPins[relay-1], state == ON ? HIGH : LOW);
    relayState[relay-1] = state;
}

void send200OK() {
    bfill.emit_p(PSTR(
                "HTTP/1.0 200 OK\r\n"
                "\r\n"));
    ether.httpServerReply(bfill.position());
}

void send404NotFound() {
    bfill.emit_p(PSTR(
                "HTTP/1.0 404 Not Found\r\n"
                "\r\n"));
    ether.httpServerReply(bfill.position());
}

void send401Unauthorized() {
    bfill.emit_p(PSTR(
                "HTTP/1.0 401 Unauthorized\r\n"
                "WWW-Authenticate: Basic realm=\"Komakallio IP Switch\"\r\n"
                "\r\n"));
    ether.httpServerReply(bfill.position());
}

void send500Error() {
    bfill.emit_p(PSTR(
                "HTTP/1.0 500 Internal Server Error\r\n"
                "\r\n"));
    ether.httpServerReply(bfill.position());
}

void sendIndexPage() {
    bfill.emit_p(PSTR("HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: $D\r\n"
        "\r\n"), index_html_len);
    ether.httpServerReplyAck();
    ether.httpServerReply_with_flags(bfill.position(), TCP_FLAGS_ACK_V);

    const char *ptr = (const char *)index_html;
    int bytesLeft = index_html_len;
    while (bytesLeft > 0) {
        int bytesToSend = min(TRANSMIT_SIZE, bytesLeft);
        memcpy_P(ether.tcpOffset(), ptr, bytesToSend);
        if (bytesToSend == TRANSMIT_SIZE) {
            ether.httpServerReply_with_flags(TRANSMIT_SIZE,TCP_FLAGS_ACK_V);
        } else {
            ether.httpServerReply_with_flags(bytesLeft,TCP_FLAGS_ACK_V|TCP_FLAGS_FIN_V);
        }
        bytesLeft -= bytesToSend;
        ptr += bytesToSend;
    }
}

void sendRelayStatus() {
    bfill.emit_p(PSTR("HTTP/1.0 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "{\"state\":[$S,$S,$S,$S]}\r\n"), 
        relayState[0] == ON ? "true" : "false", 
        relayState[1] == ON ? "true" : "false", 
        relayState[2] == ON ? "true" : "false", 
        relayState[3] == ON ? "true" : "false");
    ether.httpServerReply(bfill.position());
}

void handleNetwork(int pos) {
    bfill = ether.tcpOffset();
    char* data = (char*)&Ethernet::buffer[pos];

    if (strlen(AUTH_PASSWORD) > 0 && !strstr(data, "Authorization: Basic " AUTH_PASSWORD)) {
        send401Unauthorized();
        return;
    }

    if (strncmp("GET / ", data, 6) == 0) {
        sendIndexPage();
    } else if (strncmp("GET /relay ", data, 11) == 0) {
        sendRelayStatus();
    } else if (strncmp("POST /relay/", data, 12) == 0) {
        State state;
        bool pulse = false;
        if (strncmp("/on", &data[13], 3) == 0) {
            state = ON;
        } else if (strncmp("/off", &data[13], 4) == 0) {
            state = OFF;
        } else if (strncmp("/pulse", &data[13], 6) == 0) {
            pulse = true;
        } else {
            send500Error();
            return;
        }

        int relay = data[12] - '0';
        if (relay >= 1 && relay <= 4) {
            if (!pulse) {
                setRelay(relay, state);
                send200OK();
            } else {
                setRelay(relay, ON);
                send200OK();
                delay(1000);
                setRelay(relay, OFF);
            }
        } else {
            send500Error();
            return;
        }
    } else {
        send404NotFound();
    }
}

void handleAlnitakSerialTraffic() {
    if (Serial.available() < 6)
        return;

    char str[20];
    memset(str, 0, 20);
    
    Serial.readBytesUntil('\n', str, 20);

    char* cmd = &str[1];
    char* data = &str[2];
    char temp[16];
    
    switch(*cmd)
    {
        case 'P':
            sprintf(temp, "*P%d000", deviceId);
            Serial.println(temp);
            break;

        case 'L':
            sprintf(temp, "*L%d000", deviceId);
            Serial.println(temp);
            setRelay(1, ON);
            break;

        case 'D':
            sprintf(temp, "*D%d000", deviceId);
            Serial.println(temp);
            setRelay(1, OFF);
            break;

        case 'B':
            sprintf(temp, "*B%d%03d", deviceId, atoi(data));
            Serial.println(temp);
            break;

        case 'J':
            sprintf(temp, "*J%d%03d", deviceId, 255);
            Serial.println(temp);
            break;
      
        case 'S': 
            sprintf(temp, "*S%d%d%d%d",deviceId, 0, lightStatus, 1);
            Serial.println(temp);
            break;

        case 'V':
            sprintf(temp, "*V%d001", deviceId);
            Serial.println(temp);
            break;
    }    

    while (Serial.available() > 0)
        Serial.read();
}

void loop() {
    handleAlnitakSerialTraffic();
    int pos = ether.packetLoop(ether.packetReceive());
    if (pos) {
        handleNetwork(pos);
    }
}

