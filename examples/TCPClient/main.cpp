#include <Arduino.h>

#include <SPI.h>
#include <LoRa.h>
#include <ESP8266WiFi.h>

#include "../LoRaConfig.h"

#include <LRTP.hpp>

// The TCP port to connect to
#define TCP_PORT 8023
// the IP address to connect to
const char *tcpHost = "192.168.1.12";

// wifi access point credentials
const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PASSWORD";

// this node will have address 2
LRTP lrtp(2);

// store a reference to the currently connected node's connection
std::shared_ptr<LRTPConnection> testCon = nullptr;

// Use WiFiClient class to create TCP connections
WiFiClient tcpClient;

void setupWifi()
{
    Serial.print("\nConnecting to SSID: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        Serial.print(".");
    }
    IPAddress myIP = WiFi.localIP();
    Serial.print("\nMy IP address: ");
    Serial.println(myIP);
}

bool connectTcp()
{
    Serial.print("[TCP] Connecting to: ");
    Serial.print(tcpHost);
    Serial.print(':');
    Serial.println(TCP_PORT);

    unsigned int max_retries = 25;
    unsigned int retries = 0;
    while (!tcpClient.connect(tcpHost, TCP_PORT))
    {
        Serial.println("[TCP] Connection failed (wait 5 sec...)");
        delay(5000);
        if (retries > max_retries)
        {
            return false;
        }
        retries++;
    }
    Serial.println("[TCP] Connected!");
    return true;
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
void newPacket()
{
    Serial.println("[LRTP] Packet was received!\n");
}

void newConnection(std::shared_ptr<LRTPConnection> connection)
{
    Serial.printf("[LRTP] New connection from %u!\n", connection->getRemoteAddr());
    connection->onDataReceived(newPacket);
    testCon = connection;
}

void setup()
{
    Serial.setRxBufferSize(512);
    Serial.begin(115200);
    while (!Serial)
    {
    }
    setupWifi();
    if (!setupLoRa())
    {
        Serial.println("[SETUP] Failed to start LoRa");
    }
    Serial.println("[SETUP] Starting LoRa...");
    setupLoRaConfig();
    lrtp.begin();

    lrtp.onConnect(newConnection);

    Serial.println("\nReady.");
}

void lrtpConnectionLoop()
{
    if (testCon == nullptr)
    {
        Serial.println("[LRTP] Connecting to 1...");
        testCon = lrtp.connect(1);
    }
}
void tcpLoop()
{
    if (tcpClient.connected())
    {
        if (testCon != nullptr)
        {
            if (testCon->availableForWrite() > -1)
            {
                // read all data from LoRa connection
                size_t len = testCon->available();
                if (len > 0)
                {
                    uint8_t lBuf[len];
                    testCon->readBytes(lBuf, len);
                    tcpClient.write(lBuf, len);
                }
                // read all data from TCP
                while (testCon->availableForWrite() > 0 && tcpClient.available())
                {
                    testCon->write(tcpClient.read());
                }
            }
        }
    }
}

void loop()
{
    // Run the LRTP main loop:
    lrtp.loop();
    if (tcpClient.connected())
    {
        // if the TCP client is connected to the HTTP server, we can send data to/from it from the LRTP connection:
        lrtpConnectionLoop();
        tcpLoop();
    }
    else
    {
        delay(250);
        connectTcp();
    }
}