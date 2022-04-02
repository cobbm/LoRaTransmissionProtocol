#ifndef __LRTPCONNECTION_HPP__
#define __LRTPCONNECTION_HPP__

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

    // packet handling methods
    void handleIncomingPacket(const LRTPPacket &packet);

    LRTPPacket *getNextTxPacket();

    uint16_t getRemoteAddr();

private:
    // connection variables
    uint16_t m_srcAddr;
    uint16_t m_destAddr;
    uint8_t m_seqNum;
    uint8_t m_ackNum;

    // outgoing packet buffer
    // CircularBuffer<LRTPBufferItem> m_txBuffer;
    CircularBuffer<uint8_t> m_txDataBuffer;
    CircularBuffer<LRTPPacket> m_txPacketBuff;

    // incoming data buffer
    uint8_t m_rxBuffer[LRTP_MAX_PAYLOAD_SZ * LRTP_RX_BUFFER_SZ];
    size_t m_rxBuffPos = 0;
    size_t m_rxBuffLen = 0;

    LRTPPacket *prepareNextTxPacket();
    // LRTPBufferItem *m_currTxBuffer = nullptr;
    LRTPConnState m_state;
};

#endif