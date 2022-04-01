#pragma once

#include <Arduino.h>
#include <unordered_map>
#include <memory>

#include <SPI.h>
#include <LoRa.h>

#include "LRTPConstants.hpp"
#include "LRTPConnection.hpp"

struct LRTPFlags
{
    bool syn;
    bool fin;
    bool ack;
};

struct LRTPPacket
{
    uint8_t version;
    uint8_t type;
    LRTPFlags flags;
    uint8_t ackWindow;
    uint16_t src;
    uint16_t dest;
    uint8_t seqNum;
    uint8_t ackNum;
    uint8_t *payload;
    size_t payload_length;
};

class LRTP
{
public:
    LRTP(uint16_t hostAddr);

    LRTPConnection *connect(uint16_t destAddr);
    int begin();
    void loop();

    /**
     * @brief Parse a raw packet into the struct outPacket from a buffer of given length
     *
     * @param outPacket pointer to a LRTPPacket struct in which to parse the packet into
     * @param buf pointer to the buffer to try to parse a packet from
     * @param len the length of the raw packet in bytes
     * @return int 1 on success, 0 on failure
     */
    static int parsePacket(LRTPPacket *outPacket, uint8_t *buf, size_t len);

    static int parseHeaderFlags(LRTPFlags *outFlags, uint8_t rawFlags);

    static uint8_t packFlags(const LRTPFlags &flags);

private:
    std::unordered_map<uint16_t, std::unique_ptr<LRTPConnection>> m_activeConnections;

    uint16_t hostAddr;

    int loraRxBytesWaiting = 0;

    uint8_t m_rxBuffer[LRTP_MAX_PACKET * LRTP_GLOBAL_RX_BUFFER_SZ];

    void handleIncomingPacket(const LRTPPacket &packet);

    // handlers for LoRa async
    void onLoRaPacketReceived(int packetSize);
    void onLoRaTxDone();
    void onLoRaCADDone();
};

void debug_print_packet(const LRTPPacket &packet);