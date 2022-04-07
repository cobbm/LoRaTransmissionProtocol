#pragma once
#include <Arduino.h>
// #include "Stream.h"

#include "LRTPConstants.hpp"
#include "CircularBuffer.hpp"

class LRTP;

class LRTPConnection : public Stream
{
public:
    LRTPConnection(uint16_t source, uint16_t destination);

    // Stream implementation
    int read() override;
    int available() override;
    int peek() override;

    // Print implementation
    virtual size_t write(uint8_t val) override;
    virtual size_t write(const uint8_t *buf, size_t size) override;
    using Print::write; // include "Print" methods
    virtual void flush() override;

    /**
     * @brief Checks if the current connection is ready to transmit a packet or not
     *
     * @return true if the current connection can send another packet
     * @return false if no data or control packets are ready to send
     */
    bool isReadyForTransmit(unsigned long t);

    // packet handling methods
    void handleIncomingPacket(const LRTPPacket &packet);

    LRTPPacket *getNextTxPacket(unsigned long t);

    uint16_t getRemoteAddr();

private:
    // connection variables
    uint16_t m_srcAddr;
    uint16_t m_destAddr;

    uint8_t m_nextSeqNum;

    uint8_t m_currentAckNum;
    uint8_t m_expectedAckNum;

    uint8_t m_seqBase;
    // uint8_t m_nextSeqNum;
    uint8_t m_windowSize;
    unsigned long m_timer_packetTimeout = 0;
    bool m_timer_packetTimeoutActive = false;

    uint8_t m_remoteWindowSize = 0;

    uint8_t m_packetRetries = 0;

    bool m_sendPiggybackPacket = false;
    LRTPFlags m_piggybackFlags;
    // outgoing packet buffer
    // CircularBuffer<LRTPBufferItem> m_txBuffer;
    CircularBuffer<uint8_t> m_txDataBuffer;
    CircularBuffer<LRTPPacket> m_txWindow;

    LRTPPacket m_piggybackPacket;
    // incoming data buffer
    uint8_t m_rxBuffer[LRTP_MAX_PAYLOAD_SZ * LRTP_RX_PACKET_BUFFER_SZ];
    size_t m_rxBuffPos = 0;
    size_t m_rxBuffLen = 0;

    LRTPConnState m_state;

    LRTPPacket *getNextTxPacketData();

    // LRTPPacket *prepareNextTxPacket();

    void setTxPacketHeader(LRTPPacket &packet);

    LRTPPacket *preparePiggybackPacket();
    // LRTPBufferItem *m_currTxBuffer = nullptr;

    void handleIncomingPacketHeader(const LRTPPacket &packet);
};
