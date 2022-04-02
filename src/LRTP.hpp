#ifndef __LRTP_HPP__
#define __LRTP_HPP__

#include <Arduino.h>
#include <unordered_map>
#include <memory>

#include <SPI.h>
#include <LoRa.h>

#include "LRTPConstants.hpp"
#include "LRTPConnection.hpp"

enum class LoRaState
{
    IDLE_RECEIVE,
    RECEIVE,
    CAD_STARTED,
    CAD_FINISHED,
    TRANSMIT
};

class LRTP
{
public:
    LRTP(uint16_t m_hostAddr);

    std::shared_ptr<LRTPConnection> connect(uint16_t destAddr);

    int begin();

    void loop();

    /**
     * @brief Set a handler to be called when a new client has connected
     *
     */
    void onConnect(std::function<void(std::shared_ptr<LRTPConnection>)> callback);

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
    std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection> > m_activeConnections;

    uint16_t m_hostAddr;

    int m_loraRxBytesWaiting = 0;

    LoRaState m_currentLoRaState;

    std::function<void(std::shared_ptr<LRTPConnection>)> _onConnect = nullptr;

    uint8_t m_rxBuffer[LRTP_MAX_PACKET * LRTP_GLOBAL_RX_BUFFER_SZ];

    void loopReceive();

    void loopTransmit();

    void handleIncomingPacket(const LRTPPacket &packet);

    void handleIncomingConnectionPacket(const LRTPPacket &packet);

    void sendPacket(const LRTPPacket &packet);

    // handlers for LoRa async
    void onLoRaPacketReceived(int packetSize);
    void onLoRaTxDone();
    void onLoRaCADDone();
};

void debug_print_packet(const LRTPPacket &packet);

#endif