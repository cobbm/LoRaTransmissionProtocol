#include "LRTPConnection.hpp"

// #include "CircularBuffer.hpp"

LRTPConnection::LRTPConnection(uint16_t source, uint16_t destination)
    : m_srcAddr(source), m_destAddr(destination), m_currentSeqNum(0), m_nextAckNum(0), m_seqBase(0), m_windowSize(LRTP_TX_PACKET_BUFFER_SZ),
      m_txDataBuffer(LRTP_MAX_PAYLOAD_SZ * LRTP_TX_PACKET_BUFFER_SZ), m_txWindow(m_windowSize), m_connectionState(LRTPConnState::CLOSED) {
    // set piggyback packet data to default
    m_piggybackPacket.payloadLength = 0;
    m_piggybackPacket.payload = nullptr;
    m_piggybackPacket.version = LRTP_DEFAULT_VERSION;
    m_piggybackPacket.payloadType = LRTP_DEFAULT_TYPE;
    m_piggybackPacket.src = m_srcAddr;
    m_piggybackPacket.dest = m_destAddr;
}

LRTPConnection::~LRTPConnection() {
#if LRTP_DEBUG > 0
    Serial.println("LRTPConnection: Destructor called");
#endif
    // free the payload buffers for all packets still in the transmit buffer
    LRTPPacket *p = m_txWindow.dequeue();
    while (p != nullptr) {
        if (p->payload != nullptr) {
            free(p->payload);
        }
        p = m_txWindow.dequeue();
    }
}

// Stream implementation
int LRTPConnection::read() {
    if (m_rxBuffLen > 0 && m_rxBuffLen - m_rxBuffPos > 0) {
        return m_rxBuffer[m_rxBuffPos++];
    }
    return -1;
}
int LRTPConnection::available() {
    if (m_rxBuffLen <= 0) {
        return 0;
    } else {
        return m_rxBuffLen - m_rxBuffPos;
    }
}
int LRTPConnection::peek() {
    if (m_rxBuffLen > 0 && m_rxBuffLen - m_rxBuffPos > 0) {
        return m_rxBuffer[m_rxBuffPos];
    }
    return -1;
}
// end stream implementation

// Print implementation
size_t LRTPConnection::write(uint8_t val) {
    // try and append the byte to the data buffer
    return m_txDataBuffer.enqueue(val);
}

size_t LRTPConnection::write(const uint8_t *buf, size_t size) {
    // Serial.printf("Stream wrote (str): %s\n", buf);
    lrtp_info("Data was written to LRTP connection");
    // TODO: Improve this
    unsigned int i;
    for (i = 0; i < size; i++) {
        if (!write(buf[i]))
            break;
    }
    return i;
}

void LRTPConnection::flush() {
}

int LRTPConnection::availableForWrite() {
    // data may be written in either connected or connect_syn_ack state
    if (m_connectionState == LRTPConnState::CONNECT_SYN_ACK || m_connectionState == LRTPConnState::CONNECTED) {
        return m_txDataBuffer.size() - m_txDataBuffer.count();
    }
    return -1;
};

// end print implementation

bool LRTPConnection::connect() {
    if (m_connectionState != LRTPConnState::CLOSED)
        return false;

    // set up a new random sequence number
    m_currentSeqNum = random(0, 256);
    m_seqBase = m_currentSeqNum;
    // send SYN-ACK
    m_piggybackFlags = {
        .syn = true,
        .fin = false,
        .ack = false,
    };
    m_sendPiggybackPacket = true;
    // start timeout timer
    startPacketTimeoutTimer();
    // set state to CONNECT_SYN
    setConnectionState(LRTPConnState::CONNECT_SYN);
    return true;
}

bool LRTPConnection::close() {
    /*
    1. wait for all pending packets to be send & add FIN flag to final packet
       --  OR  --
       send empty/piggyback FIN packet
    2. set CLOSE_FIN state
    3. handle FIN/ACK packet from remote host (in HandleCloseFin)
    4. - set CLOSED state
    */

    setConnectionState(LRTPConnState::CLOSE_FIN);
    return true;
}

void LRTPConnection::onDataReceived(std::function<void(void)> callback) {
    m_onDataReceived = callback;
}

void LRTPConnection::onClose(std::function<void(void)> callback) {
    m_onClose = callback;
}

uint16_t LRTPConnection::getRemoteAddr() {
    return m_destAddr;
}

LRTPConnState LRTPConnection::getConnectionState() {
    return m_connectionState;
}

void LRTPConnection::updateTimers(unsigned long t) {
    // check if timers have elapsed
    if (m_timer_packetTimeoutActive && t - m_timer_packetTimeout > LRTP_PACKET_TIMEOUT) {
        onPacketTimeout();
    }
    // handle piggyback timeout
    if (m_timer_piggybackTimeoutActive && t - m_timer_piggybackTimeout > LRTP_PIGGYBACK_TIMEOUT) {
        onPiggybackTimeout();
    }
}

bool LRTPConnection::isReadyForTransmit() {
    // we can transmit a packet if there is data in the send buffer, or if we need
    // to send a control packet
    bool dataWaitingForTransmit = m_txDataBuffer.count() > 0;
    bool connectionOpen = m_connectionState == LRTPConnState::CONNECTED;

    uint8_t positionInWindow = m_currentSeqNum - m_seqBase;

    bool canTransmitData = connectionOpen && ((dataWaitingForTransmit && positionInWindow < m_windowSize) || (positionInWindow < m_txWindow.count()));

    lrtp_infof("dataWaiting: %u, connectionOpen: %u, canTransmit: %u, sendPiggyback: %u\n",
        dataWaitingForTransmit,
        connectionOpen,
        canTransmitData,
        m_sendPiggybackPacket);
    return m_sendPiggybackPacket || canTransmitData;
}

LRTPPacket *LRTPConnection::prepareNextPacket() {
    lrtp_infof("Creating LRTP Packet, payload size: %u bytes\n", m_txDataBuffer.count());
    // check if we're connected
    if (!(m_connectionState == LRTPConnState::CONNECTED /*|| m_connectionState == LRTPConnState::CONNECT_SYN ||
            m_connectionState == LRTPConnState::CONNECT_SYN_ACK*/)) {
        lrtp_infof("NOT CONNECTED\n");
        return nullptr;
    }
    // check that there is data waiting to transmit and that there is space inside
    // the transmit window to queue the packet
    size_t bytesWaiting = m_txDataBuffer.count();
    if (bytesWaiting > 0 && m_txWindow.count() < m_windowSize) {
        // get the next free packet in the queue
        LRTPPacket *nextPacket = m_txWindow.enqueueEmpty();
        if (nextPacket != nullptr) {
            const int packetPayloadSz = min(m_txDataBuffer.count(), (size_t)LRTP_MAX_PAYLOAD_SZ);
            if (packetPayloadSz > 0) {
                lrtp_infof(" TODO: arena allocator/stack?\n");
                // TODO: arena allocator/stack?
                uint8_t *payloadBuff = (uint8_t *)malloc(sizeof(uint8_t) * packetPayloadSz);

                nextPacket->payload = payloadBuff;
                // copy payload to packet struct
                for (int i = 0; i < packetPayloadSz; i++) {
                    *(payloadBuff++) = *m_txDataBuffer.dequeue();
                }
            } else {
                lrtp_infof("Packet with no payload\n");
                nextPacket->payload = nullptr;
            }
            // set "static" header fields
            nextPacket->payloadLength = packetPayloadSz;
            nextPacket->version = LRTP_DEFAULT_VERSION;
            nextPacket->payloadType = LRTP_DEFAULT_TYPE;
            nextPacket->src = m_srcAddr;
            nextPacket->dest = m_destAddr;

            return nextPacket;
        } else {
            lrtp_infof("Could not enqueue empty packet\n");
        }
    } else {
        lrtp_infof("Could not get next packet\n");
    }
    return nullptr;
}

LRTPPacket *LRTPConnection::getNextTxPacket() {
    lrtp_info("getNextTxPacket() begin");

    LRTPPacket *nextPacket = nullptr;
    const uint8_t relativeSeqNo = m_currentSeqNum - m_seqBase;

    // TODO: What is this?
    lrtp_infof("LRTP State: relativeSeqNo %u (m_currentSeqNum (%u) - (m_seqBase (%u)) < m_windowSize (%u)\n",
        relativeSeqNo,
        m_currentSeqNum,
        m_seqBase,
        m_windowSize);

    // implement ARQ Go Back N
    if (relativeSeqNo < m_windowSize) {
        lrtp_infof("Assertion: (relativeSeqNo < m_windowSize): entire window not sent yet. check if we are ready for the next packet. relativeSeqNo (%d) < "
                   "m_txWindow.count() (%d)\n",
            relativeSeqNo,
            m_txWindow.count());
        // entire window not sent yet
        // check if we are ready for the next packet
        if (relativeSeqNo < m_txWindow.count() && m_txWindow.count() > 0) {
            // get packet from buffer
            nextPacket = m_txWindow[relativeSeqNo];
        } else {
            // fill buffer with next packet to send
            nextPacket = prepareNextPacket();
            if (!nextPacket) {
                lrtp_info("prepareNextPacket returned null!");
            }
        }
    } else {
        // TODO: What is this?
        lrtp_infof("TODO: relativeSeqNo %u (m_currentSeqNum (%u) - (m_seqBase (%u)) < m_windowSize (%u)\n",
            relativeSeqNo,
            m_currentSeqNum,
            m_seqBase,
            m_windowSize);
    }

    if (nextPacket == nullptr && m_sendPiggybackPacket) {
        lrtp_infof("SENDING PIGGYBACK");
        nextPacket = &m_piggybackPacket;
    }
    if (nextPacket != nullptr) {

        // set FIN flag on final packet if closing connection
        if (m_connectionState == LRTPConnState::CLOSE_FIN && relativeSeqNo >= m_txWindow.count()) {
#if LRTP_DEBUG > 0
            // Serial.printf("%s: FINAL PACKET Seq: %u\n", __PRETTY_FUNCTION__, m_currentSeqNum);
#endif
            lrtp_infof("FINAL PACKET Seq: %u\n", m_currentSeqNum);
            m_sendPiggybackPacket = true;
            m_piggybackFlags.fin = true;
        }

        setTxPacketHeader(*nextPacket);
        if (nextPacket->payloadLength > 0) {
            incrementSeqNum();

            // start the timeout timer
            startPacketTimeoutTimer();
        }
    } else {
#if LRTP_DEBUG > 2
        Serial.printf("ERROR: Could not get next packet!\n");
#endif
    }
    return nextPacket;
}

void LRTPConnection::setTxPacketHeader(LRTPPacket &packet) {
    lrtp_infof("setTxPacketHeader begin! %u\n", m_sendPiggybackPacket);
    // set "dynamic" header fields (i.e. may change between retransmits)
    if (m_sendPiggybackPacket) {
        // TODO: Handle piggybacking
        packet.flags = m_piggybackFlags;
        // we have handled the piggybacking
        m_sendPiggybackPacket = false;
        // stop the piggyback timer
        m_timer_piggybackTimeoutActive = false;
    } else {
        packet.flags.ack = true;
        packet.flags.syn = false;
        packet.flags.fin = false;
    }
    packet.ackWindow = m_windowSize;

    packet.seqNum = m_currentSeqNum;
    packet.ackNum = m_nextAckNum;
}

bool LRTPConnection::handleStateClosed(const LRTPPacket &packet) {
    lrtp_info("handleStateClosed begin");

    if (packet.flags.syn && !packet.flags.ack && packet.payloadLength == 0) {
        // set up acknowledgement number
        m_nextAckNum = packet.seqNum + 1;
        // set random sequence number
        m_currentSeqNum = random(0, 256);

        m_seqBase = m_currentSeqNum;
        // send SYN-ACK
        m_piggybackFlags = {
            .syn = true,
            .fin = false,
            .ack = true,
        };
        m_sendPiggybackPacket = true;
        // set state to CONNECT_SYN_ACK
        setConnectionState(LRTPConnState::CONNECT_SYN_ACK);
        // TODO: timeout
        startPacketTimeoutTimer();
        return true;
    } else {
        // Serial.printf("%s: ERROR: Invalid SYN packet\n", __PRETTY_FUNCTION__);
        m_connectionError = LRTPError::INVALID_SYN;
        setConnectionState(LRTPConnState::CLOSED);
        return false;
    }
}

bool LRTPConnection::handleStateConnectSYN(const LRTPPacket &packet) {
    lrtp_info("handleStateConnectSYN begin");
    if (packet.flags.syn && packet.flags.ack && packet.ackNum == m_currentSeqNum + 1) {
        m_nextAckNum = packet.seqNum + 1;

        incrementSeqNum();
        m_seqBase = m_currentSeqNum;
        // send ACK (& data)
        m_piggybackFlags = {
            .syn = false,
            .fin = false,
            .ack = true,
        };
        // stop packet timeout timer
        m_timer_packetTimeoutActive = false;

        startPiggybackTimeoutTimer();
        // set state to connected
        setConnectionState(LRTPConnState::CONNECTED);
        return true;
    } else {
        lrtp_info("ERROR: Invalid SYN/ACK packet. Resend SYN\n");

        // Should be called -> LRTPError::INVALID_SYN_ACK; ?but name clashes with
        // next error.
        ////TODO find out how to name them differently

        m_connectionError = LRTPError::INVALID_SYN_ACK_SYN;

        // invalid packet, resend SYN (ACK?)
        m_piggybackFlags = {
            .syn = true,
            .fin = false,
            .ack = false,
        };
        m_sendPiggybackPacket = true;
        // start packet timeout:
        startPacketTimeoutTimer();
        return false;
    }
    return false;
}

bool LRTPConnection::handleStateConnectSYNACK(const LRTPPacket &packet) {
    lrtp_info("handleStateConnectSYNACK() begin");

    if (packet.flags.ack && packet.seqNum == m_nextAckNum) {
        incrementSeqNum();
        m_seqBase = m_currentSeqNum;
        setConnectionState(LRTPConnState::CONNECTED);
        return true;
    } else {
        lrtp_info("ERROR: Invalid ACK packet. Resend SYN/ACK\n");
        m_connectionError = LRTPError::INVALID_SYN_ACK;
        // invalid packet, resend SYN ACK
        m_piggybackFlags = {
            .syn = true,
            .fin = false,
            .ack = true,
        };
        m_sendPiggybackPacket = true;
        // TODO timeout:
        startPacketTimeoutTimer();

        return false;
    }
}

bool LRTPConnection::handleStateConnected(const LRTPPacket &packet) {
    lrtp_info("handleStateConnected() begin");

    const bool hasPayload = packet.payloadLength > 0;

    if (packet.seqNum == m_nextAckNum) {
        // valid packet
        if (hasPayload) {
            // m_currentAckNum = m_nextAckNum;
            m_nextAckNum++;
        }
        if (packet.flags.ack) {
            handlePacketAckFlag(packet);
        }
        if (packet.flags.fin) {
            setConnectionState(LRTPConnState::CLOSE_FIN);
        }
        if (hasPayload) {
            // we need to send an ACK for this payload
            m_piggybackFlags = {
                .syn = false,
                .fin = false,
                .ack = true,
            };
            // start piggyback timer
            startPiggybackTimeoutTimer();
        }
        return true;
    } else {
        Serial.printf("%s: WARNING: Invalid Sequence number: %u\n", __PRETTY_FUNCTION__, packet.seqNum);
        // packet has an invalid sequence number
        // send an ack for the last acknowledged sequence number to trigger a full
        // resend of the remote transmit window.
        m_piggybackFlags = {
            .syn = false,
            .fin = false,
            .ack = true,
        };
        // TODO: start piggyback timer
        startPiggybackTimeoutTimer();
        return false;
    }
}

void LRTPConnection::handleIncomingPacket(const LRTPPacket &packet) {
#if LRTP_DEBUG > 1
    Serial.printf(" ==== Handle %s === \n", connStateToStr(m_connectionState));
#endif
    bool validPacket = false;
    switch (m_connectionState) {
    case LRTPConnState::CLOSED:
        validPacket = handleStateClosed(packet);
        break;
    case LRTPConnState::CONNECT_SYN:
        validPacket = handleStateConnectSYN(packet);
        break;
    case LRTPConnState::CONNECT_SYN_ACK:
        validPacket = handleStateConnectSYNACK(packet);
        // if we receive a valid ACK we can process this packet in connected state
        if (validPacket)
            validPacket = handleStateConnected(packet);
        break;
    case LRTPConnState::CONNECTED:
        validPacket = handleStateConnected(packet);
        break;
    case LRTPConnState::CLOSE_FIN:
        validPacket = handleStateConnected(packet);
        // if (validPacket) validPacket= handleStateCloseFIN(packet);
        break;

    case LRTPConnState::CLOSE_FIN_ACK:
        Serial.printf(" ==== Error: Unimplemented State %s ===\n", connStateToStr(m_connectionState));
        m_connectionError = LRTPError::CLOSE_FIN_ACK;
        break;
    default:
        m_connectionError = LRTPError::INVALID_STATE;
        Serial.printf("%s: Invalid state: %s\n", __PRETTY_FUNCTION__, connStateToStr(m_connectionState));
    }
    // copy payload into rx buffer
    if (validPacket && packet.payloadLength > 0) {
        // TODO: use circular buffer on receive side too?
        memcpy(m_rxBuffer, packet.payload, sizeof(uint8_t) * packet.payloadLength);
        m_rxBuffLen = packet.payloadLength;
        m_rxBuffPos = 0;
        // call the callback function for this connection
        if (m_onDataReceived != nullptr) {
            m_onDataReceived();
        }
    }
}

void LRTPConnection::setConnectionState(LRTPConnState newState) {
#if LRTP_DEBUG > 1
    Serial.printf("%s: Connection change state: %s -> %s\n", __PRETTY_FUNCTION__, connStateToStr(m_connectionState), connStateToStr(newState));
#endif
    m_connectionState = newState;
}

void LRTPConnection::advanceSendWindow(uint16_t ackNum) {
    uint16_t longSeqBase = m_seqBase;
    while (longSeqBase < ackNum) {
        // advance sliding window
        LRTPPacket *oldPacket = m_txWindow.dequeue();
        if (oldPacket != nullptr) {
#if LRTP_DEBUG > 1
            Serial.printf("%s: Acknowledge Seq: %u\n", __PRETTY_FUNCTION__, oldPacket->seqNum);
#endif
            if (oldPacket->payload != nullptr) {
                // free malloced payload buffer
                free(oldPacket->payload);
            }
            longSeqBase++;
        }
    }
    // Serial.printf("===== advanceSendWindow() m_currentSeqNum = %u =====\n",
    // m_seqBase);
    m_seqBase = longSeqBase;
    m_currentSeqNum = m_seqBase;
}

void LRTPConnection::handlePacketAckFlag(const LRTPPacket &packet) {
    // stop send timeout timer
    m_timer_packetTimeoutActive = false;

    const size_t sendWindowCount = m_txWindow.count();
    const uint16_t sendWindowEnd = m_seqBase + sendWindowCount;

    uint16_t adjustedAckNum = packet.ackNum;
    const bool ackMayOverflow = (sendWindowEnd & 0xff) < m_seqBase;
    if (ackMayOverflow)
        adjustedAckNum += 256;

    if (sendWindowCount > 0) {
        if (adjustedAckNum >= m_seqBase && adjustedAckNum <= sendWindowEnd) {
            if (adjustedAckNum < sendWindowEnd) {
                /*Serial.printf("%s: Entire window not ack'd - Packet ACK: %u, Base: %u, "
                              "Window end: %u \n",
                    __PRETTY_FUNCTION__,
                    packet.ackNum,
                    m_seqBase,
                    sendWindowEnd);
*/

                lrtp_infof("Entire window not ack'd - Packet ACK: %u, Base: %u, Window end: %u \n", packet.ackNum, m_seqBase, sendWindowEnd);

                // entire window was not acknowledged
                // restart timeout timer
                startPacketTimeoutTimer();
            }
#if LRTP_DEBUG > 2
            Serial.printf("Acknowledge %u -> %u\n", m_seqBase, packet.ackNum);
#endif
            advanceSendWindow(adjustedAckNum);
            m_packetRetries = 0;
        } else {
            // resend entire window
            Serial.printf("===== m_currentSeqNum = %u, seqBase = $u ===== (RESEND ENTIRE WINDOW) \n", m_currentSeqNum, m_seqBase);
            m_currentSeqNum = m_seqBase;
        }
    }
}

void LRTPConnection::startPacketTimeoutTimer() {
#if LRTP_DEBUG > 1
    Serial.printf("== Start Packet Timeout Timer ==\n");
#endif
    m_timer_packetTimeoutActive = true;
    m_timer_packetTimeout = millis();
}
void LRTPConnection::onPacketTimeout() {
    // handle timeout
#if LRTP_DEBUG > 0
    Serial.printf("== Packet TIMEOUT [%u] ==\n", m_packetRetries);
#endif
    lrtp_infof("== Packet m_currentSeqNum: %u, seqBase: %u. TIMEOUT [retries %u of ?] ==\n", m_currentSeqNum, m_seqBase, m_packetRetries);
    if (m_connectionState == LRTPConnState::CONNECT_SYN || m_connectionState == LRTPConnState::CONNECT_SYN_ACK) {
        m_sendPiggybackPacket = true;
        startPacketTimeoutTimer();
    } else {
        // reset nextsequencenumber to the start of the window
        m_currentSeqNum = m_seqBase;
        // m_timer_packetTimeout = t;
        m_timer_packetTimeoutActive = false;
    }
    m_packetRetries++;
}

void LRTPConnection::startPiggybackTimeoutTimer() {
#if LRTP_DEBUG > 1
    Serial.printf("== Start Piggyback Timeout Timer ==\n");
#endif
    m_timer_piggybackTimeoutActive = true;
    m_timer_piggybackTimeout = millis();
}
void LRTPConnection::onPiggybackTimeout() {
#if LRTP_DEBUG > 1
    Serial.printf("== Piggyback Timer TIMEOUT ==\n");
#endif
    m_timer_piggybackTimeoutActive = false;
    m_sendPiggybackPacket = true;
}

void LRTPConnection::incrementSeqNum() {
    m_currentSeqNum++;
}

/* ========== Debug methods ==========*/
const char *connStateToStr(LRTPConnState state) {
    switch (state) {
    case LRTPConnState::CLOSED:
        return "CLOSED";
        break;
    case LRTPConnState::CONNECT_SYN:
        return "CONNECT_SYN";
        break;
    case LRTPConnState::CONNECT_SYN_ACK:
        return "CONNECT_SYN_ACK";
        break;
    case LRTPConnState::CONNECTED:
        return "CONNECTED";
        break;
    case LRTPConnState::CLOSE_FIN:
        return "CLOSE_FIN";
        break;
    case LRTPConnState::CLOSE_FIN_ACK:
        return "CLOSE_FIN_ACK";
        break;
    default:
        return "INVALID";
        break;
    }
}
