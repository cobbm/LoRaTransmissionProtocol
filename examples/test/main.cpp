#include <Arduino.h>

#include <SPI.h>
#include <LoRa.h>

#include "LoRaConfig.h"

#include <LRTP.hpp>

LRTP lrtp(1);

std::shared_ptr<LRTPConnection> testCon = nullptr;

unsigned long lastTx = 0;
const unsigned long txEvery = 5000;

bool setupLoRa()
{
    // SPI LoRa pins
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    // setup LoRa transceiver module
    LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
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

#ifdef ARDUINO_TTGO_LoRa32_v21new
    // TTGO has output connected to PA_OUTPUT_RFO_PIN
    // LoRa.setTxPower(17, PA_OUTPUT_PA_BOOST_PIN);
#endif
}

void newConnection(std::shared_ptr<LRTPConnection> connection)
{
    Serial.printf("New connection from %u!!!\n", connection->getRemoteAddr());
    testCon = connection;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
    }
    if (!setupLoRa())
    {
        Serial.println("Failed to start LoRa");
    }
    setupLoRaConfig();
    lrtp.begin();

    lrtp.onConnect(newConnection);
}

void loop()
{
    lrtp.loop();
    if (testCon != nullptr)
    {
        unsigned long t = millis();
        if (t - lastTx >= txEvery)
        {
            Serial.printf("Wrote some data...");
            //  write some data
            testCon->println("HELLO WORLD TESTING!");
            lastTx = t;
        }
        // read some data
        if (testCon->available() > 0)
        {
            Serial.print("Reading data: ");
            while (testCon->available() > 0)
            {
                Serial.print((char)testCon->read());
            }
            Serial.println();
        }
    }
}