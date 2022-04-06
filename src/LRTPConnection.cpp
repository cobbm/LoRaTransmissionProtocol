#include "LRTPConnection.hpp"

// #include "CircularBuffer.hpp"

LRTPConnection::LRTPConnection(uint16_t source, uint16_t destination) : m_srcAddr(source), m_destAddr(destination), m_nextSeqNum(0), m_ackNum(0), m_seqBase(0), m_windowSize(LRTP_TX_PACKET_BUFFER_SZ), m_txDataBuffer(LRTP_MAX_PAYLOAD_SZ * LRTP_TX_PACKET_BUFFER_SZ), m_txWindow(m_windowSize), m_state(LRTPConnState::CLOSED)
{
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
    Serial.printf("Stream wrote (str): %s\n", buf);
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

bool LRTPConnection::isReadyForTransmit(unsigned long t)
{
    // we can transmit a packet if there is data in the send buffer, or if we need to send a control packet
    bool packetTimeout = m_timer_packetTimeoutActive && t - m_timer_packetTimeout > LRTP_PACKET_TIMEOUT;
    bool dataWaitingForTransmit = m_txDataBuffer.count() > 0;
    size_t positionInWindow = m_nextSeqNum - m_seqBase;

    return packetTimeout || positionInWindow < m_txWindow.count() || (m_txWindow.count() < m_windowSize && dataWaitingForTransmit) || m_piggybackPacket;
}

LRTPPacket *LRTPConnection::getNextTxPacketData()
{
    // check that there is data waiting to transmit and that there is space inside the transmit window for the packet
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
        Serial.printf("%s: === TIMEOUT %u ===\n", __PRETTY_FUNCTION__, m_packetRetries);
        // reset nextsequencenumber to the start of the window
        m_nextSeqNum = m_seqBase;
        // m_timer_packetTimeout = t;
        m_timer_packetTimeoutActive = false;
        m_packetRetries++;
    }

    // implement ARQ Go Back N
    if (m_nextSeqNum < m_seqBase + m_windowSize)
    {
        // entire window not sent yet
        LRTPPacket *nextPacket = nullptr;
        // check if we are ready for the next packet
        if (m_nextSeqNum < m_seqBase + m_txWindow.count())
        {
            // get packet from buffer
            nextPacket = m_txWindow[m_nextSeqNum - m_seqBase];
        }
        else
        {
            // fill buffer with next packet to send
            nextPacket = getNextTxPacketData();
        }
        if (nextPacket != nullptr)
        {
            setTxPacketHeader(*nextPacket);
            // start the timeout timer
            m_timer_packetTimeoutActive = true;
            m_timer_packetTimeout = t;
            // increment sequence number
            m_nextSeqNum += 1;
        }
        else if (m_piggybackPacket)
        {
            // we need to send a piggyback packet
            Serial.printf("%s: TODO: Implement piggybacking!\n", __PRETTY_FUNCTION__);
            m_piggybackPacket = false;
        }
        else
        {
            Serial.printf("%s: ERROR: Packet was null!\n", __PRETTY_FUNCTION__);
        }
        return nextPacket;
    }
    Serial.printf("ERROR: Could not get next packet!\n");
    return nullptr;
}

void LRTPConnection::setTxPacketHeader(LRTPPacket &packet)
{
    // set "dynamic" header fields (i.e. may change between retransmits)
    // handle flags elsewhere?
    if (m_piggybackPacket)
    {
        // TODO: Handle piggybacking
        m_piggybackPacket = false;
    }

    packet.flags.ack = false;
    packet.flags.syn = false;
    packet.flags.fin = false;

    packet.ackWindow = m_windowSize;

    packet.seqNum = m_nextSeqNum;
    packet.ackNum = m_ackNum;
}

void LRTPConnection::handleIncomingPacket(const LRTPPacket &packet)
{

    handleIncomingPacketHeader(packet);
    // m_ackNum = packet.seqNum + 1;
    if (packet.payload_length > 0)
    {
        // copy payload into rx buffer
        // TODO: use circular buffer?
        memcpy(m_rxBuffer, packet.payload, sizeof(uint8_t) * packet.payload_length);
        m_rxBuffLen = packet.payload_length;
        m_rxBuffPos = 0;
    }
}

void LRTPConnection::handleIncomingPacketHeader(const LRTPPacket &packet)
{
    if (packet.flags.ack)
    {
        uint8_t ackWindowIndex = packet.ackNum - m_seqBase;
        if (ackWindowIndex >= 0 && ackWindowIndex < m_txWindow.count())
        {
            if (packet.ackNum == m_seqBase + m_txWindow.count() - 1)
            {
                // stop timer
                m_timer_packetTimeoutActive = false;
            }
            else
            {
                // start timer
                m_timer_packetTimeoutActive = true;
                m_timer_packetTimeout = millis();
            }

            // advance the sliding window base to acknowledge these packets and remove them from the sliding window queue
            while (m_seqBase <= packet.ackNum)
            {
                Serial.printf("ACKNOWLEDGED SEQ: %u PACKET\n", m_seqBase);
                LRTPPacket *oldPacket = m_txWindow.dequeue();
                if (oldPacket != nullptr)
                {
                    // free the payload buffer stored inside this packet
                    if (oldPacket->payload_length > 0 && oldPacket->payload != nullptr)
                    {
                        free(oldPacket->payload);
                    }
                }
                ackWindowIndex--;
                m_seqBase++;
            }
            // m_seqBase = packet.ackNum + 1;
            // TODO: Check this part:
            if (m_nextSeqNum < m_seqBase)
                m_nextSeqNum = m_seqBase;
        }
        else
        {
            Serial.printf("%s: ERROR(?) Packet ACK was out of bounds!\n", __PRETTY_FUNCTION__);
        }
    }
}

uint16_t LRTPConnection::getRemoteAddr()
{
    return m_destAddr;
}