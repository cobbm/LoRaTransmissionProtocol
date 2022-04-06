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
    uint16_t m_hostAddr;

    LoRaState m_currentLoRaState;

    unsigned int m_cadRoundsRemaining = 0;

    bool m_channelActive = false;

    // the number of bytes waiting to be read by a connection currently in the receive buffer
    int m_loraRxBytesWaiting = 0;

    // stores the next connection which has a packet waiting to transmit, so it can be used after channel activity detection completes
    LRTPConnection *m_nextConnectionForTransmit = nullptr;

    // a buffer used to hold bytes read from the radio that have not yet been processed by a connection
    uint8_t m_rxBuffer[LRTP_MAX_PACKET * LRTP_GLOBAL_RX_BUFFER_SZ];

    // map from connection address to connection object. used to dispatch data to the correct connection once it has been received.
    std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection>> m_activeConnections;

    unsigned int m_checkReceiveRounds = 0;
    unsigned long m_timer_checkReceiveTimeout = 0;

    // event handlers
    std::function<void(std::shared_ptr<LRTPConnection>)> _onConnect = nullptr;

    // handles receiveing data from the radio during the update loop
    void loopReceive();
    // handles the transmision of a packet during the loop
    void loopTransmit();

    /**
     * @brief starts channel activity detection before transmitting a packet. switches to CAD_STARTED state.
     * If this is called while a packet is being received, switches state to RECEIVE and doesn't start CAD
     * (otherwise the packet being received will be dropped by the LoRa radio.)
     *
     * @return true if CAD was successfully started
     * @return false if we're part way through receiving a packet
     */
    bool beginCAD();

    void handleIncomingPacket(const LRTPPacket &packet);

    void handleIncomingConnectionPacket(const LRTPPacket &packet);

    // sends a packet once CAD has finished
    void sendPacket(const LRTPPacket &packet);

    // handlers for LoRa async
    void onLoRaPacketReceived(int packetSize);
    void onLoRaTxDone();
    void onLoRaCADDone(bool channelBusy);
};

void debug_print_packet(const LRTPPacket &packet);

#endif