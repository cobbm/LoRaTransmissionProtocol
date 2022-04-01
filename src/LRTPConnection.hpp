#pragma once
#include <Arduino.h>
#include "Stream.h"

#include "LRTPConstants.hpp"
#include "CircularBuffer.hpp"

struct LRTPBufferItem
{
    int sentTries = 0;
    bool acknowledged = false;
    size_t length = 0;
    uint8_t buffer[LRTP_MAX_PAYLOAD_SZ];
};

enum class LRTPConnState
{
    CLOSED,
    CONNECT_SYN,
    CONNECT_SYN_ACK,
    CONNECTED,
    CLOSE_FIN,
    CLOSE_FIN_ACK,
    CLOSED_END
};

class LRTPConnection : public Stream
{
public:
    LRTPConnection(uint16_t destination);

    // Stream implementation
    int read() override;
    int available() override;
    int peek() override;

    // Print implementation
    virtual size_t write(uint8_t val) override;
    virtual size_t write(const uint8_t *buf, size_t size) override;
    using Print::write; // include "Print" methods
    virtual void flush() override;

private:
    // connection variables
    uint16_t m_destAddr;
    uint16_t m_srcAddr;
    uint8_t m_seqNum;
    uint8_t m_ackNum;
    // outgoing packet buffer
    CircularBuffer<LRTPBufferItem> m_txBuffer;
    // incoming data buffer
    uint8_t m_rxBuffer[LRTP_MAX_PAYLOAD_SZ * LRTP_RX_BUFFER_SZ];
    size_t m_rxBuffPos = 0;
    size_t m_rxBuffLen = 0;

    LRTPBufferItem *m_currTxBuffer = nullptr;
    LRTPConnState m_state;
};