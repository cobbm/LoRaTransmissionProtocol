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

    /**
     * @brief Checks if the current connection is ready to transmit a packet or not
     *
     * @return true if the current connection can send another packet
     * @return false if no data or control packets are ready to send
     */
    bool isReadyForTransmit();

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

    uint8_t m_seqBase;
    // uint8_t m_nextSeqNum;
    uint8_t m_windowSize;
    unsigned long m_packetTimeout = 0;
    bool m_packetTimeoutStarted = false;

    // outgoing packet buffer
    // CircularBuffer<LRTPBufferItem> m_txBuffer;
    CircularBuffer<uint8_t> m_txDataBuffer;
    CircularBuffer<LRTPPacket> m_unackedPackets;

    // incoming data buffer
    uint8_t m_rxBuffer[LRTP_MAX_PAYLOAD_SZ * LRTP_RX_PACKET_BUFFER_SZ];
    size_t m_rxBuffPos = 0;
    size_t m_rxBuffLen = 0;

    LRTPConnState m_state;

    // void fillTxBuffer();

    LRTPPacket *prepareNextTxPacket();

    void setTxPacketHeader(LRTPPacket &packet);
    // LRTPBufferItem *m_currTxBuffer = nullptr;

    void handleIncomingPacketFlags(const LRTPPacket &packet);
};

#endif