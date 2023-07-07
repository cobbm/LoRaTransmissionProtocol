#pragma once
#include <Arduino.h>
// #include "Stream.h"
#include <functional>

#include "CircularBuffer.hpp"
#include "LRTPConstants.hpp"

#include "LRTPDebug.h"

class LRTP;

class LRTPConnection : public Stream {
  public:
    // constructor
    LRTPConnection(uint16_t source, uint16_t destination);
    // destructor
    ~LRTPConnection();

    // Stream implementation
    int read() override;
    int available() override;
    int peek() override;

    // Print implementation
    virtual size_t write(uint8_t val) override;
    virtual size_t write(const uint8_t *buf, size_t size) override;
    using Print::write; // include "Print" methods
    virtual void flush() override;
    virtual int availableForWrite() override;

    /**
     * @brief opens the connection if it is currently closed (Sends SYN packet)
     *
     * @return true if the operation succeeded
     * @return false if the operation failed
     */
    bool connect();

    bool close();
    /**
     * @brief Attaches a callback to be called when a new packet is received for
     * this connection
     *
     * @param callback the callback to attach. The packet payload can be read by
     * calling read()
     */
    void onDataReceived(std::function<void(void)> callback);

    /**
     * @brief Attach a callback hander to be called when the connection closes
     *
     * @param the function to call on close
     */
    void onClose(std::function<void(void)> callback);

    uint16_t getRemoteAddr();

    LRTPConnState getConnectionState();

    // private:
    void updateTimers(unsigned long t);
    /**
     * @brief Checks if the current connection is ready to transmit a packet or
     * not
     *
     * @return true if the current connection can send another packet
     * @return false if no data or control packets are ready to send
     */
    bool isReadyForTransmit();

    // packet handling methods
    void handleIncomingPacket(const LRTPPacket &packet);

    // LRTPPacket *getNextTxPacket(unsigned long t);
    LRTPPacket *getNextTxPacket();

    LRTPError m_connectionError = LRTPError::NONE;

  private:
    //  connection variables
    uint16_t m_srcAddr;
    uint16_t m_destAddr;

    uint8_t m_currentSeqNum;

    uint8_t m_nextAckNum;

    uint8_t m_seqBase;
    uint8_t m_windowSize;

    uint8_t m_remoteWindowSize = 0;

    uint8_t m_packetRetries = 0;
    // timer to handle packet timeout
    unsigned long m_timer_packetTimeout = 0;
    bool m_timer_packetTimeoutActive = false;

    // timer to handle piggybacking of flags
    unsigned long m_timer_piggybackTimeout = 0;
    bool m_timer_piggybackTimeoutActive = false;

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

    LRTPConnState m_connectionState;

    // callbacks
    std::function<void(void)> m_onClose = nullptr;
    std::function<void(void)> m_onDataReceived = nullptr;

    // private methods
    LRTPPacket *prepareNextPacket();

    // LRTPPacket *prepareNextTxPacket();

    void setTxPacketHeader(LRTPPacket &packet);

    void setConnectionState(LRTPConnState newState);

    void startPacketTimeoutTimer();
    void onPacketTimeout();
    void startPiggybackTimeoutTimer();
    void onPiggybackTimeout();
    // LRTPPacket *preparePiggybackPacket();
    // LRTPBufferItem *m_currTxBuffer = nullptr;
    bool handleStateClosed(const LRTPPacket &packet);
    bool handleStateConnectSYN(const LRTPPacket &packet);
    bool handleStateConnectSYNACK(const LRTPPacket &packet);
    bool handleStateConnected(const LRTPPacket &packet);

    void handlePacketAckFlag(const LRTPPacket &packet);
    void advanceSendWindow(uint16_t ackNum);

    inline void incrementSeqNum();
};

/* ========== Debug methods ==========*/
const char *connStateToStr(LRTPConnState state);
