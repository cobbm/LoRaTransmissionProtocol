#include "LRTPConnection.hpp"

// #include "CircularBuffer.hpp"

LRTPConnection::LRTPConnection(uint16_t source, uint16_t destination) : m_srcAddr(source), m_destAddr(destination), m_nextSeqNum(0), m_nextAckNum(0), m_currentAckNum(m_nextAckNum - 1), m_seqBase(0), m_windowSize(LRTP_TX_PACKET_BUFFER_SZ), m_txDataBuffer(LRTP_MAX_PAYLOAD_SZ * LRTP_TX_PACKET_BUFFER_SZ), m_txWindow(m_windowSize), m_connectionState(LRTPConnState::CLOSED)
{
    // set piggyback packet data to default
    m_piggybackPacket.payloadLength = 0;
    m_piggybackPacket.payload = nullptr;
    m_piggybackPacket.version = LRTP_DEFAULT_VERSION;
    m_piggybackPacket.payloadType = LRTP_DEFAULT_TYPE;
    m_piggybackPacket.src = m_srcAddr;
    m_piggybackPacket.dest = m_destAddr;
}

// Stream implementation
int LRTPConnection::read()
{
    if (m_rxBuffLen > 0 && m_rxBuffLen - m_rxBuffPos > 0)
    {
        return m_rxBuffer[m_rxBuffPos++];
    }
    return -1;
}
int LRTPConnection::available()
{
    if (m_rxBuffLen <= 0)
    {
        return 0;
    }
    else
    {
        return m_rxBuffLen - m_rxBuffPos;
    }
}
int LRTPConnection::peek()
{
    if (m_rxBuffLen > 0 && m_rxBuffLen - m_rxBuffPos > 0)
    {
        return m_rxBuffer[m_rxBuffPos];
    }
    return -1;
}
// end stream implementation

// Print implementation
size_t LRTPConnection::write(uint8_t val)
{
    // try and append the byte to the data buffer
    return m_txDataBuffer.enqueue(val);
}

size_t LRTPConnection::write(const uint8_t *buf, size_t size)
{
    // Serial.printf("Stream wrote (str): %s\n", buf);
    // TODO: Improve this
    int i;
    for (i = 0; i < size; i++)
    {
        if (!write(buf[i]))
            break;
    }
    return i;
}

void LRTPConnection::flush() {}

// end print implementation

bool LRTPConnection::connect()
{
    if (m_connectionState != LRTPConnState::CLOSED)
        return false;

    // set up a new random sequence number
    m_nextSeqNum = random(0, 256);
    m_seqBase = m_nextSeqNum;
    // send SYN-ACK
    m_piggybackFlags = {
        .syn = true,
        .fin = false,
        .ack = false,
    };
    m_sendPiggybackPacket = true;
    // set state to CONNECT_SYN
    setConnectionState(LRTPConnState::CONNECT_SYN);
    return true;
}

bool LRTPConnection::close()
{
    /*
    1. wait for all pending packets to be send & add FIN flag to final packet
       --  OR  --
       send empty/piggyback FIN packet
    2. set CLOSE_FIN state
    3. handle FIN/ACK packet from remote host (in HandleCloseFin)
    4. - set CLOSED state
    */

    setConnectionState(LRTPConnState::CLOSE_FIN);
}

void LRTPConnection::updateTimers(unsigned long t)
{
    // check if timers have elapsed
    if (m_timer_packetTimeoutActive && t - m_timer_packetTimeout > LRTP_PACKET_TIMEOUT)
    {
        onPacketTimeout();
    }
    // handle piggyback timeout
    if (m_timer_piggybackTimeoutActive && t - m_timer_piggybackTimeout > LRTP_PIGGYBACK_TIMEOUT)
    {
        onPiggybackTimeout();
    }
}

bool LRTPConnection::isReadyForTransmit()
{
    // we can transmit a packet if there is data in the send buffer, or if we need to send a control packet
    // bool packetTimeout = m_timer_packetTimeoutActive && t - m_timer_packetTimeout >= LRTP_PACKET_TIMEOUT;
    // bool piggybackTimeout = m_timer_piggybackTimeoutActive && t - m_timer_piggybackTimeout >= LRTP_PIGGYBACK_TIMEOUT;
    bool dataWaitingForTransmit = m_txDataBuffer.count() > 0;
    bool connectionOpen = m_connectionState == LRTPConnState::CONNECTED;

    size_t positionInWindow = m_nextSeqNum - m_seqBase;

    bool canTransmitData = connectionOpen && dataWaitingForTransmit && (positionInWindow < m_txWindow.count() || m_txWindow.count() < m_windowSize);

    return /*packetTimeout || piggybackTimeout ||*/ m_sendPiggybackPacket || canTransmitData;
}

LRTPPacket *LRTPConnection::prepareNextPacket()
{
    // check if we're connected
    if (m_connectionState != LRTPConnState::CONNECTED /*|| m_connectionState != LRTPConnState::CLOSE_FIN*/)
        return nullptr;
    // check that there is data waiting to transmit and that there is space inside the transmit window to queue the packet
    size_t bytesWaiting = m_txDataBuffer.count();
    if (bytesWaiting > 0 && m_txWindow.count() < m_windowSize)
    {
        // get the next free packet in the queue
        LRTPPacket *nextPacket = m_txWindow.enqueueEmpty();
        if (nextPacket != nullptr)
        {
            const int packetPayloadSz = min(m_txDataBuffer.count(), (size_t)LRTP_MAX_PAYLOAD_SZ);
            if (packetPayloadSz > 0)
            {
                // TODO: Better approach than malloc?
                uint8_t *payloadBuff = (uint8_t *)malloc(sizeof(uint8_t) * packetPayloadSz);

                nextPacket->payload = payloadBuff;
                // copy payload to packet struct
                for (int i = 0; i < packetPayloadSz; i++)
                {
                    *(payloadBuff++) = *m_txDataBuffer.dequeue();
                }
            }
            else
            {
                nextPacket->payload = nullptr;
            }
            // set "static" header fields
            nextPacket->payloadLength = packetPayloadSz;
            nextPacket->version = LRTP_DEFAULT_VERSION;
            nextPacket->payloadType = LRTP_DEFAULT_TYPE;
            nextPacket->src = m_srcAddr;
            nextPacket->dest = m_destAddr;

            return nextPacket;
        }
    }
    return nullptr;
}

LRTPPacket *LRTPConnection::getNextTxPacket()
{
    /*
    // handle timeout
    if (m_timer_packetTimeoutActive && t - m_timer_packetTimeout > LRTP_PACKET_TIMEOUT)
    {
#if LRTP_DEBUG > 0
        Serial.printf("%s: === TIMEOUT %u ===\n", __PRETTY_FUNCTION__, m_packetRetries);
#endif
        // reset nextsequencenumber to the start of the window
        m_nextSeqNum = m_seqBase;
        // m_timer_packetTimeout = t;
        m_timer_packetTimeoutActive = false;
        m_packetRetries++;
    }
    // handle piggyback timeout
    if (m_timer_piggybackTimeoutActive && t - m_timer_piggybackTimeout > LRTP_PIGGYBACK_TIMEOUT)
    {
        m_timer_packetTimeoutActive = false;
        m_sendPiggybackPacket = true;
    }
    */

    /*
        LRTPPacket *nextPacket = nullptr;
        // implement ARQ Go Back N
        if (m_nextSeqNum < m_seqBase + m_windowSize)
        {
            // entire window not sent yet

            // check if we are ready for the next packet
            if (m_nextSeqNum < m_seqBase + m_txWindow.count())
            {
                // get packet from buffer
                nextPacket = m_txWindow[m_nextSeqNum - m_seqBase];
            }
            else
            {
                // fill buffer with next packet to send
                nextPacket = prepareNextPacket();
            }
            if (nextPacket != nullptr)
            {
                // check if we have started a timer for piggyback packets.
                // if so, we can stop the timer and send the data along with this packet
                if (m_timer_piggybackTimeoutActive)
                {
                    m_timer_piggybackTimeoutActive = false;
                    m_sendPiggybackPacket = true;
                }

                setTxPacketHeader(*nextPacket);
                // start the packet timeout timer
                startPacketTimeoutTimer();
                // increment sequence number
                m_nextSeqNum += 1;
                // return nextPacket;
            }
            else
            {
    #if LRTP_DEBUG > 2
                Serial.printf("%s: Warning: Packet was null!\n", __PRETTY_FUNCTION__);
    #endif
                //  return nullptr;
            }
        }
        else if (m_sendPiggybackPacket)
        {
            // check if we need to send a piggyback packet (piggyback timer has elapsed)
    #if LRTP_DEBUG > 2
            Serial.printf("%s: piggybacking!\n", __PRETTY_FUNCTION__);
    #endif
            // we need to force send a piggyback packet.
            // these packets don't need to be buffered as they do not increment sequence number and are not acknowledged by the other party

            setTxPacketHeader(m_piggybackPacket);
            nextPacket = &m_piggybackPacket;
            // m_sendPiggybackPacket = false;
        }
    #if LRTP_DEBUG > 2
        if (nextPacket == nullptr)
            Serial.printf("ERROR: Could not get next packet!\n");
    #endif
        return nextPacket;

        */
    LRTPPacket *nextPacket = nullptr;
    // implement ARQ Go Back N
    if (m_nextSeqNum < m_seqBase + m_windowSize)
    {
        // entire window not sent yet

        // check if we are ready for the next packet
        if (m_nextSeqNum < m_seqBase + m_txWindow.count())
        {
            // get packet from buffer
            nextPacket = m_txWindow[m_nextSeqNum - m_seqBase];
        }
        else
        {
            // fill buffer with next packet to send
            nextPacket = prepareNextPacket();
        }
    }
    else if (m_sendPiggybackPacket)
    {
        nextPacket = &m_piggybackPacket;
    }
    if (nextPacket != nullptr)
    {
        setTxPacketHeader(*nextPacket);
        // set FIN flag on final packet if closing connection
        if (m_connectionState == LRTPConnState::CLOSE_FIN && m_nextSeqNum >= m_seqBase + m_txWindow.count())
        {
            m_sendPiggybackPacket = true;
            m_piggybackFlags.fin = true;
        }
    }
    else
    {
#if LRTP_DEBUG > 2
        Serial.printf("ERROR: Could not get next packet!\n");
#endif
    }
    return nextPacket;
}

void LRTPConnection::setTxPacketHeader(LRTPPacket &packet)
{
    // set "dynamic" header fields (i.e. may change between retransmits)
    // handle flags elsewhere?
    if (m_sendPiggybackPacket)
    {
        // TODO: Handle piggybacking
        packet.flags = m_piggybackFlags;
        /*
        m_piggybackFlags.ack = false;
        m_piggybackFlags.fin = false;
        m_piggybackFlags.syn = false;
        */
        // we have handled the piggybacking
        m_sendPiggybackPacket = false;
        // stop the piggyback timer
        m_timer_piggybackTimeoutActive = false;
    }
    else
    {
        packet.flags.ack = true;
        packet.flags.syn = false;
        packet.flags.fin = false;
    }

    packet.ackWindow = m_windowSize;

    packet.seqNum = m_nextSeqNum;
    packet.ackNum = m_currentAckNum;
}

bool LRTPConnection::handleStateClosed(const LRTPPacket &packet)
{
    Serial.printf(" ==== Handle %s === \n", connStateToStr(m_connectionState));
    if (packet.flags.syn && packet.payloadLength == 0)
    {
        // set up acknowledgement number
        m_currentAckNum = packet.seqNum;
        m_nextAckNum = packet.seqNum + 1;
        // set random sequence number
        m_nextSeqNum = random(0, 256);
        m_seqBase = m_nextSeqNum;
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
        // startPacketTimeoutTimer();
        return true;
    }
    else
    {
        Serial.printf("%s: ERROR: Invalid SYN packet\n", __PRETTY_FUNCTION__);
        // setConnectionState(LRTPConnState::CLOSE_END);
        setConnectionState(LRTPConnState::CLOSED);
        return false;
    }
}

bool LRTPConnection::handleStateConnectSYN(const LRTPPacket &packet)
{
    Serial.printf(" ==== Handle %s === \n", connStateToStr(m_connectionState));
    return false;
}

bool LRTPConnection::handleStateConnectSYNACK(const LRTPPacket &packet)
{
    Serial.printf(" ==== Handle %s === \n", connStateToStr(m_connectionState));
    // check if this is a repeat of the previous SYN packet
    //  if (packet.flags.syn && packet.seqNum == m_currentAckNum){
    //      //resend SYN ACK
    //      m_piggybackFlags = {
    //          .syn = true,
    //          .fin = false,
    //          .ack = true,
    //      };
    //      m_sendPiggybackPacket = true;
    //      //TODO timeout:

    //     return false;
    // }
    Serial.printf("m_currentAckNum: %u, m_nextAckNum: %u\n", m_currentAckNum, m_nextAckNum);
    Serial.printf("m_nextSeqNum: %u\n", m_nextSeqNum);
    if (packet.flags.ack && packet.seqNum == m_nextAckNum)
    {
        m_nextSeqNum++;
        m_seqBase = m_nextSeqNum;
        setConnectionState(LRTPConnState::CONNECTED);
        return true;
    }
    else
    {
        Serial.printf("%s: Invalid packet! resend SYN ACK\n", __PRETTY_FUNCTION__);
        // invalid packet, resend SYN ACK
        m_piggybackFlags = {
            .syn = true,
            .fin = false,
            .ack = true,
        };
        m_sendPiggybackPacket = true;
        // TODO timeout:
        // startPacketTimeoutTimer();

        return false;
    }
}

bool LRTPConnection::handleStateConnected(const LRTPPacket &packet)
{
    Serial.printf(" ==== Handle %s === \n", connStateToStr(m_connectionState));
    const bool hasPayload = packet.payloadLength > 0;

    if (packet.seqNum == m_nextAckNum)
    {
        // valid packet
        if (hasPayload)
        {
            m_currentAckNum = m_nextAckNum;
            m_nextAckNum++;
        }
        if (packet.flags.ack)
        {
            handlePacketAckFlag(packet);
        }
        if (hasPayload)
        {
            // we need to send an ACK for this payload
            m_piggybackFlags.ack = true;
            // start piggyback timer
            startPiggybackTimeoutTimer();
        }
        return true;
    }
    else
    {
        // packet has an invalid sequence number
        // send an ack for the last acknowledged sequence number to trigger a full resend of the remote transmit window.
        m_piggybackFlags.ack = true;
        // TODO: start piggyback timer
        startPiggybackTimeoutTimer();
        return false;
    }
}

void LRTPConnection::handleIncomingPacket(const LRTPPacket &packet)
{
    bool validPacket = false;
    switch (m_connectionState)
    {
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

    case LRTPConnState::CLOSE_FIN_ACK:

        // case LRTPConnState::CLOSE_END:
        Serial.printf(" ==== Error: UNIMPLEMENTED STATE %s ===\n", connStateToStr(m_connectionState));
        break;
    default:
        Serial.printf("%s: Invalid state: %s\n", __PRETTY_FUNCTION__, connStateToStr(m_connectionState));
    }
    // copy payload into rx buffer
    if (validPacket && packet.payloadLength > 0)
    {
        // TODO: use circular buffer on receive side too?
        memcpy(m_rxBuffer, packet.payload, sizeof(uint8_t) * packet.payloadLength);
        m_rxBuffLen = packet.payloadLength;
        m_rxBuffPos = 0;
        // call the callback function for this connection
        if (m_onDataReceived != nullptr)
        {
            m_onDataReceived();
        }
    }
}

void LRTPConnection::setConnectionState(LRTPConnState newState)
{
#if LRTP_DEBUG > 0
    Serial.printf("%s: Connection change state: %s -> %s\n", __PRETTY_FUNCTION__, connStateToStr(m_connectionState), connStateToStr(newState));
#endif
    m_connectionState = newState;
}

void LRTPConnection::onDataReceived(std::function<void()> callback)
{
    m_onDataReceived = callback;
}

uint16_t LRTPConnection::getRemoteAddr()
{
    return m_destAddr;
}

LRTPConnState LRTPConnection::getConnectionState()
{
    return m_connectionState;
}

void LRTPConnection::advanceSendWindow(uint8_t ackNum)
{
    while (m_seqBase <= ackNum)
    {
        // advance sliding window
        LRTPPacket *oldPacket = m_txWindow.dequeue();
        if (oldPacket != nullptr)
        {
            if (oldPacket->payload != nullptr)
            {
                // free malloced payload buffer
                free(oldPacket->payload);
            }
            m_seqBase++;
        }
    }
    m_nextSeqNum = m_seqBase;
    m_packetRetries = 0;
    //} else if (packet.ackNum == ((m_seqBase-1) & 0xff)){
    //    //resend the entire window
    //}
}

void LRTPConnection::handlePacketAckFlag(const LRTPPacket &packet)
{
    // stop send timeout timer
    m_timer_packetTimeoutActive = false;

    const size_t sendWindowCount = m_txWindow.count();
    const int sendWindowEnd = m_seqBase + (sendWindowCount - 1);
    if (sendWindowCount > 0)
    {
        if (packet.ackNum >= m_seqBase && packet.ackNum <= sendWindowEnd)
        {
            if (packet.ackNum < sendWindowEnd)
            {
                // entire window was not acknowledged
                // restart timeout timer
                startPacketTimeoutTimer();
            }
#if LRTP_DEBUG > 2
            Serial.printf("Acknowledge %u -> %u\n", m_seqBase, packet.ackNum);
#endif
            advanceSendWindow(packet.ackNum);
        }
        else
        {
            // resend entire window
            m_nextSeqNum = m_seqBase;
        }
    }
}

void LRTPConnection::startPacketTimeoutTimer()
{
    m_timer_packetTimeoutActive = true;
    m_timer_packetTimeout = millis();
}
void LRTPConnection::onPacketTimeout()
{
    // handle timeout
#if LRTP_DEBUG > 0
    Serial.printf("%s: === TIMEOUT %u ===\n", __PRETTY_FUNCTION__, m_packetRetries);
#endif
    // reset nextsequencenumber to the start of the window
    m_nextSeqNum = m_seqBase;
    // m_timer_packetTimeout = t;
    m_timer_packetTimeoutActive = false;
    m_packetRetries++;
}
void LRTPConnection::startPiggybackTimeoutTimer()
{
    m_timer_packetTimeoutActive = true;
    m_timer_piggybackTimeout = millis();
}
void LRTPConnection::onPiggybackTimeout()
{
    m_timer_packetTimeoutActive = false;
    m_sendPiggybackPacket = true;
}

/* ========== Debug methods ==========*/
const char *connStateToStr(LRTPConnState state)
{
    switch (state)
    {
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
    // case LRTPConnState::CLOSE_END:
    //     return "CLOSE_END";
    // break;
    default:
        return "INVALID";
        break;
    }
}