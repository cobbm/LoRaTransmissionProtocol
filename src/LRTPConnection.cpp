#include "LRTPConnection.hpp"

// #include "CircularBuffer.hpp"

LRTPConnection::LRTPConnection(uint16_t source, uint16_t destination) : m_srcAddr(source), m_destAddr(destination), m_nextSeqNum(0), m_nextAckNum(0), m_currentAckNum(m_nextAckNum - 1), m_seqBase(0), m_windowSize(LRTP_TX_PACKET_BUFFER_SZ), m_txDataBuffer(LRTP_MAX_PAYLOAD_SZ * LRTP_TX_PACKET_BUFFER_SZ), m_txWindow(m_windowSize), m_state(LRTPConnState::CLOSED)
{
    m_piggybackPacket.payload_length = 0;
    m_piggybackPacket.payload = nullptr;
    m_piggybackPacket.version = LRTP_DEFAULT_VERSION;
    m_piggybackPacket.type = LRTP_DEFAULT_TYPE;
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

void LRTPConnection::update(unsigned long t)
{
}

bool LRTPConnection::isReadyForTransmit(unsigned long t)
{
    // we can transmit a packet if there is data in the send buffer, or if we need to send a control packet
    bool packetTimeout = m_timer_packetTimeoutActive && t - m_timer_packetTimeout >= LRTP_PACKET_TIMEOUT;
    bool piggybackTimeout = m_timer_piggybackTimeoutActive && t - m_timer_piggybackTimeout >= LRTP_PIGGYBACK_TIMEOUT;
    bool dataWaitingForTransmit = m_txDataBuffer.count() > 0;
    size_t positionInWindow = m_nextSeqNum - m_seqBase;

    return packetTimeout || positionInWindow < m_txWindow.count() || (m_txWindow.count() < m_windowSize && dataWaitingForTransmit) || piggybackTimeout || m_sendPiggybackPacket;
}

LRTPPacket *LRTPConnection::prepareNextPacket()
{
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
            nextPacket->payload_length = packetPayloadSz;
            nextPacket->version = LRTP_DEFAULT_VERSION;
            nextPacket->type = LRTP_DEFAULT_TYPE;
            nextPacket->src = m_srcAddr;
            nextPacket->dest = m_destAddr;

            return nextPacket;
        }
    }
    return nullptr;
}

LRTPPacket *LRTPConnection::getNextTxPacket(unsigned long t)
{
    // handle timeout
    if (m_timer_packetTimeoutActive && t - m_timer_packetTimeout > LRTP_PACKET_TIMEOUT)
    {
#if LRTP_DEBUG > 2
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
                m_sendPiggybackPacket = true;
            setTxPacketHeader(*nextPacket);
            // start the timeout timer
            m_timer_packetTimeoutActive = true;
            m_timer_packetTimeout = t;
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
    // check if we need to send a piggyback packet
    //(piggyback timer has elapsed)
    if (m_sendPiggybackPacket)
    {
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
}

void LRTPConnection::setTxPacketHeader(LRTPPacket &packet)
{
    // set "dynamic" header fields (i.e. may change between retransmits)
    // handle flags elsewhere?
    if (m_sendPiggybackPacket)
    {
        // TODO: Handle piggybacking
        packet.flags = m_piggybackFlags;
        m_piggybackFlags.ack = false;
        m_piggybackFlags.fin = false;
        m_piggybackFlags.syn = false;
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

bool LRTPConnection::handleStateConnectSYN(const LRTPPacket &packet)
{
}

bool LRTPConnection::handleStateConnectSYNACK(const LRTPPacket &packet)
{
}

bool LRTPConnection::handleStateConnected(const LRTPPacket &packet)
{
    const bool hasPayload = packet.payload_length > 0;

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
                        m_timer_packetTimeoutActive = true;
                        m_timer_packetTimeout = millis();
                    }
#if LRTP_DEBUG > 2
                    Serial.printf("Acknowledge %u -> %u\n", m_seqBase, packet.ackNum);
#endif
                    while (m_seqBase <= packet.ackNum)
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
                else
                {
                    // resend entire window
                    m_nextSeqNum = m_seqBase;
                }
            }
        }
        if (hasPayload)
        {
            // we need to send an ACK for this payload
            m_piggybackFlags.ack = true;
            // start piggyback timer
            m_timer_piggybackTimeout = millis();
            m_timer_piggybackTimeoutActive = true;
        }
        return true;
    }
    else
    {
        // packet has an invalid sequence number
        // send an ack for the last acknowledged sequence number to trigger a full resend of the remote transmit window.
        m_piggybackFlags.ack = true;
        // TODO: start piggyback timer
        m_timer_piggybackTimeout = millis();
        m_timer_piggybackTimeoutActive = true;
        return false;
    }
}

void LRTPConnection::handleIncomingPacket(const LRTPPacket &packet)
{

    bool validPacket = handleStateConnected(packet);
    // m_nextAckNum = packet.seqNum + 1;
    if (validPacket && packet.payload_length > 0)
    {
        // copy payload into rx buffer
        // TODO: use circular buffer?
        memcpy(m_rxBuffer, packet.payload, sizeof(uint8_t) * packet.payload_length);
        m_rxBuffLen = packet.payload_length;
        m_rxBuffPos = 0;
        if (m_onDataReceived != nullptr)
        {
            m_onDataReceived();
        }
    }
}

void LRTPConnection::onDataReceived(std::function<void()> callback)
{
    m_onDataReceived = callback;
}

uint16_t LRTPConnection::getRemoteAddr()
{
    return m_destAddr;
}