#include <Arduino.h>

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>

#include "../LoRaConfig.h"

#include <LRTP.hpp>

#define MAX_TELNET_BUF 247
#define TELNET_BUFFER_DUR 750

// Port to open the TCP socket on
#define TCP_PORT 8023

// how many clients should be able to telnet to this ESP32
#define MAX_SRV_CLIENTS 1
const char *ssid = "ESP32-01";
const char *password = "PASSWORD_HERE";

// create a TCP server
WiFiServer server(TCP_PORT);
WiFiClient serverClients[MAX_SRV_CLIENTS];

size_t lastTelnetAvailable = 0;
unsigned long lastTelnetRead = 0;

// this node has address 1
LRTP lrtp(1);

std::shared_ptr<LRTPConnection> testCon = nullptr;

void setupWifi()
{
    Serial.print("SSID: ");
    Serial.println(ssid);
    WiFi.softAP(ssid, password);
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(myIP);
}

void setupTelnet()
{
    server.begin();
    server.setNoDelay(true);
    server.setTimeout(15);
}

bool setupLoRa()
{
// SPI LoRa pins
#if defined(ESP32)
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
#endif
    // setup LoRa transceiver module
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    return LoRa.begin(LORA_BAND);
}

void setupLoRaConfig()
{
    // set LORA parameters
    LoRa.setSyncWord(LORA_SYNC_WORD);
#ifdef LORA_CRC
    LoRa.enableCrc();
#endif
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
    LoRa.setSignalBandwidth(LORA_SIGNAL_BANDWIDTH);
}
// handler attached to the LRTP::onDataReceived() callback
void newPacket()
{
    Serial.println("Packet was received!\n");
}

void newConnection(std::shared_ptr<LRTPConnection> connection)
{
    Serial.printf("New connection from %u!\n", connection->getRemoteAddr());
    connection->onDataReceived(newPacket);
    testCon = connection;
}

void setup()
{
    Serial.setRxBufferSize(512);
    Serial.begin(115200);

    setupWifi();
    setupTelnet();
    if (!setupLoRa())
    {
        Serial.println("Failed to start LoRa");
    }
    Serial.println("Starting LoRa...");
    setupLoRaConfig();
    lrtp.begin();

    lrtp.onConnect(newConnection);

    Serial.println("\nReady.");
}

void tcpLoop()
{
    uint8_t i;
    // check if there are any new TCP clients connecting
    if (server.hasClient())
    {
        for (i = 0; i < MAX_SRV_CLIENTS; i++)
        {
            // find free/disconnected spot
            if (!serverClients[i] || !serverClients[i].connected())
            {
                if (serverClients[i])
                {
                    serverClients[i].stop();
                }
                serverClients[i] = server.available();
                serverClients[i].setTimeout(15);
                if (!serverClients[i])
                    Serial.println("available broken");
                Serial.printf("New client: %u: %s\n", i, serverClients[i].remoteIP().toString().c_str());
                break;
            }
        }
        if (i >= MAX_SRV_CLIENTS)
        {
            // no free/disconnected spot so reject
            Serial.println("Error: No free spots available");
            server.available().stop();
        }
    }

    for (i = 0; i < MAX_SRV_CLIENTS; i++)
    {
        if (serverClients[i] && serverClients[i].connected())
        {
            // check clients for data
            if (testCon != nullptr)
            {
                // read all data
                if (serverClients[i].available())
                {
                    // get data from the TCP socket, whilst allowing it to buffer for a short time to prevent sending too many small packets
                    unsigned long t = millis();
                    if (serverClients[i].available() != lastTelnetAvailable)
                    {
                        lastTelnetRead = t;
                        lastTelnetAvailable = serverClients[i].available();
                    }
                    if (t - lastTelnetRead >= TELNET_BUFFER_DUR)
                    {
                        Serial.printf("Sending: (%u) %u\n", i, serverClients[i].available());
                        while (serverClients[i].available())
                            testCon->write(serverClients[i].read());
                        lastTelnetAvailable = 0;
                    }
                }
            }
        }
        else
        {
            if (serverClients[i])
            {
                serverClients[i].stop();
            }
        }
    }
    if (testCon != nullptr)
    {
        if (testCon->available() > 0)
        {
            size_t len = testCon->available();
            uint8_t lBuf[len];
            testCon->readBytes(lBuf, len);

            // push LRTP data to all connected telnet clients
            for (i = 0; i < MAX_SRV_CLIENTS; i++)
            {
                if (serverClients[i] && serverClients[i].connected())
                {
                    serverClients[i].write(lBuf, len);
                    delay(1);
                }
            }
        }
    }
}

void loop()
{
    lrtp.loop();
    tcpLoop();
}