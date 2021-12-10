#include <Arduino.h>
#include <EthernetENC.h>
#include <avr/wdt.h>
#include "indexhtml.h"
#include "config.h"

enum State
{
	OFF = 0,
	ON
};

#define ETHERNET_BUFFER_SIZE 600
static char data[ETHERNET_BUFFER_SIZE];


static byte macAddress[] = { 0x32, 0x9D, 0x14, 0x66, 0x7D, 0x65 };
static IPAddress IP;
static IPAddress GATEWAY;
static IPAddress dnsServer(8, 8, 8, 8);
static IPAddress netmask(255, 255, 255, 0);

static EthernetServer server(80);
static const char AUTHHEADER[] PROGMEM = "Authorization: Basic " AUTH_PASSWORD;

static int relayPins[] = { 8, 7, 6, 5 };
static State relayState[] = { OFF, OFF, OFF, OFF };
static int flatRelayPin = 8;
static int deviceId = 19; // FLAT_MAN

static unsigned long lastTick;

int lightStatus = OFF;

ISR(WDT_vect) { 
    if (millis() - lastTick > 4000) {
        goto *(0x00); 
    }
}

void resetWatchdog() {
    lastTick = millis();
}

void enableWatchdog() {
    cli();
    wdt_reset();
    WDTCSR = (1<<WDCE)|(1<<WDE);
    //Start watchdog timer with 4s prescaller
    WDTCSR = (1<<WDIE) | (1<<WDP2) | (1<<WDP0);
    sei();
}

void setup() {
    resetWatchdog();

    Serial.begin(9600);
    pinMode(flatRelayPin, OUTPUT);
    digitalWrite(flatRelayPin, LOW);

    Ethernet.init(SS);
    Ethernet.begin(macAddress, ip, dnsServer, gateway, netmask);
    
    server.begin();
    enableWatchdog();
}

void setRelay(int relay, State state) {
    digitalWrite(relayPins[relay-1], state == ON ? HIGH : LOW);
    relayState[relay-1] = state;
}

void send200OK(EthernetClient client) {
    client.println(F("HTTP/1.0 200 OK"));
    client.println();
    delay(10);
    client.stop();
}

void send400BadRequest(EthernetClient client) {
    client.println(F("HTTP/1.0 400 Bad Request"));
    client.println();
    delay(10);
    client.stop();
}


void send404NotFound(EthernetClient client) {
    client.println(F("HTTP/1.0 404 Not Found"));
    client.println();
    delay(10);
    client.stop();
}

void send401Unauthorized(EthernetClient client) {
    client.println(F("HTTP/1.0 401 Unauthorized"));
    client.println(F("WWW-Authenticate: Basic realm=\"Chilly Astronomers IP Switch\""));
    client.println();
    delay(10);
    client.stop();
}

void send500Error(EthernetClient client) {
    client.println(F("HTTP/1.0 500 Internal Server Error"));
    client.println();
    delay(10);
    client.stop();
}

void sendIndexPage(EthernetClient client) {
    client.println(F("HTTP/1.0 200 OK"));
    client.println(F("Content-Type: text/html"));
    client.print(F("Content-Length: "));
    client.println(index_html_len);
    client.println();
    const unsigned char* src = index_html;
    int bytesToSend = index_html_len;
    while (bytesToSend > 0) {
        int chunkSize = min(bytesToSend, ETHERNET_BUFFER_SIZE);
        memcpy_P(data, src, chunkSize);
        client.write(data, chunkSize);
        src += chunkSize;
        bytesToSend -= chunkSize;
    }
    client.flush();
    delay(10);
    client.stop();
}

void sendRelayStatus(EthernetClient client) {
    client.println(F("HTTP/1.0 200 OK"));
    client.println();
    sprintf_P(data, PSTR("{\"state\":[%s,%s,%s,%s]}"), 
        relayState[0] == ON ? "true" : "false", 
        relayState[1] == ON ? "true" : "false", 
        relayState[2] == ON ? "true" : "false", 
        relayState[3] == ON ? "true" : "false");
    client.println(data);
    delay(10);
    client.stop();
}

void handleRequest(EthernetClient client) {
    int pos = 0;
    unsigned long t = millis();
    while (1) {
        while (!client.available()) {
            delay(10);
            resetWatchdog();
            if ((millis() - t) > 5000) {
                send400BadRequest(client);
                return;
            }
        }

        int bytesToRead = min(client.available(), ETHERNET_BUFFER_SIZE-pos-1);
        client.readBytes(&data[pos], bytesToRead);
        pos += bytesToRead;
        data[pos] = 0;

        if (pos >= ETHERNET_BUFFER_SIZE-2 ||
            strstr_P(data, PSTR("\r\n\r\n")) || 
            strstr_P(data, PSTR("\n\n"))) {
            break;
        }
    }

    if (strlen(AUTH_PASSWORD) > 0 && !strstr_P(data, AUTHHEADER)) {
        send401Unauthorized(client);
        return;
    }

    if (strncmp("GET / ", data, 6) == 0) {
        sendIndexPage(client);
    } else if (strncmp("GET /relay ", data, 11) == 0) {
        sendRelayStatus(client);
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
            send500Error(client);
            return;
        }

        int relay = data[12] - '0';
        if (relay >= 1 && relay <= 4) {
            if (!pulse) {
                setRelay(relay, state);
                send200OK(client);
            } else {
                setRelay(relay, ON);
                send200OK(client);
                delay(1000);
                setRelay(relay, OFF);
            }
        } else {
            send500Error(client);
            return;
        }
    } else {
        send404NotFound(client);
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
            sprintf_P(temp, PSTR("*P%d000"), deviceId);
            Serial.println(temp);
            break;

        case 'L':
            sprintf_P(temp, PSTR("*L%d000"), deviceId);
            Serial.println(temp);
            setRelay(1, ON);
            break;

        case 'D':
            sprintf_P(temp, PSTR("*D%d000"), deviceId);
            Serial.println(temp);
            setRelay(1, OFF);
            break;

        case 'B':
            sprintf_P(temp, PSTR("*B%d%03d"), deviceId, atoi(data));
            Serial.println(temp);
            break;

        case 'J':
            sprintf_P(temp, PSTR("*J%d%03d"), deviceId, 255);
            Serial.println(temp);
            break;
      
        case 'S': 
            sprintf_P(temp, PSTR("*S%d%d%d%d"), deviceId, 0, lightStatus, 1);
            Serial.println(temp);
            break;

        case 'V':
            sprintf_P(temp, PSTR("*V%d001"), deviceId);
            Serial.println(temp);
            break;
    }    

    while (Serial.available() > 0)
        Serial.read();
}

void loop() {
    handleAlnitakSerialTraffic();

    EthernetClient client = server.available();
    if (client) {
        handleRequest(client);
    }
    resetWatchdog();
}

