#include "LRTPConnection.hpp"

// #include "CircularBuffer.hpp"

LRTPConnection::LRTPConnection(uint16_t source, uint16_t destination) : m_srcAddr(source), m_destAddr(destination), m_seqNum(0), m_ackNum(0), m_seqBase(0), m_windowSize(LRTP_TX_PACKET_BUFFER_SZ), m_txDataBuffer(LRTP_MAX_PAYLOAD_SZ * LRTP_TX_PACKET_BUFFER_SZ), m_unackedPackets(m_windowSize), m_state(LRTPConnState::CLOSED)
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
    // Serial.printf("Stream wrote: %c\n", val);
    /*
    if (m_currTxBuffer == nullptr || m_currTxBuffer->length >= LRTP_MAX_PAYLOAD_SZ)
    {
        // try and get a new buffer to write into
        m_currTxBuffer = m_txBuffer.enqueueEmpty();
        if (m_currTxBuffer == nullptr)
        {
            // allocation failed
            return 0;
        }
    }
    m_currTxBuffer->buffer[m_currTxBuffer->length++] = val;
    return 1;
    */

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

bool LRTPConnection::isReadyForTransmit()
{
    // we can transmit a packet if there is data in the send buffer, or if we need to send a control packet
    // return (m_txDataBuffer.count() > 0 && m_unackedPackets.count() < LRTP_TX_PACKET_BUFFER_SZ);
    unsigned long t = millis();
    bool packetTimeout = m_packetTimeoutStarted && t - m_packetTimeout > LRTP_PACKET_TIMEOUT;

    return (m_txDataBuffer.count() > 0 && m_seqNum < m_seqBase + m_windowSize) || packetTimeout;
}

/*
void LRTPConnection::fillTxBuffer()
{
    size_t bytesWaiting = m_txDataBuffer.count();

    if (bytesWaiting > 0 && m_unackedPackets.count() < LRTP_TX_PACKET_BUFFER_SZ)
    {
        //fill
    }
}
*/

LRTPPacket *LRTPConnection::getNextTxPacket()
{
    unsigned long t = millis();

    // check if any packets need to be resent
    if (m_packetTimeoutStarted && t - m_packetTimeout > LRTP_PACKET_TIMEOUT)
    {
        // first packet has timed out, reset sequence number back to base
        m_seqNum = m_seqBase;

        m_packetTimeoutStarted = false;
        Serial.printf("%s: Packet %u timeout! resending...\n", __PRETTY_FUNCTION__, m_seqNum);
    }
    uint8_t nextSeqBuffIdx = m_seqNum - m_seqBase;
    // if (nextSeqBuffIdx < m_unackedPackets.count())
    if (nextSeqBuffIdx < m_windowSize && nextSeqBuffIdx < LRTP_TX_PACKET_BUFFER_SZ)
    {
        if (nextSeqBuffIdx == 0)
        {
            // restart timeout timer
            m_packetTimeoutStarted = true;
            m_packetTimeout = t;
        }
        LRTPPacket *packet = nullptr;
        // check if the packet to send is already buffered
        if (nextSeqBuffIdx < m_unackedPackets.count())
        {
            // resend the packet
            packet = m_unackedPackets[nextSeqBuffIdx];
        }
        else
        {
            // send the next packet
            packet = prepareNextTxPacket();
        }
        setTxPacketHeader(*packet);
        // increment sequence number
        m_seqNum++;
        return packet;
    }
    else
    {
        Serial.printf("%s: Error: Tried to get packet to send out of bounds!\n", __PRETTY_FUNCTION__);
        return nullptr;
    }
}

LRTPPacket *LRTPConnection::prepareNextTxPacket()
{
    // return the next packet available for transmitting
    // try and get a new packet to buffer
    // if (m_txDataBuffer.count() <= 0)
    // {
    //     // no data waiting
    //     return nullptr;
    // }
    unsigned long t = millis();

    LRTPPacket *txPacket = m_unackedPackets.enqueueEmpty();
    if (txPacket == nullptr)
    {
        Serial.printf("%s: Error: Connection %u: TX packet buffer full\n", __PRETTY_FUNCTION__, m_destAddr);
        // allocation failed, tx buffer is full of unacknowledged packets
        return nullptr;
    }

    // construct the packet with as much data as we can take
    size_t payloadSize = min(m_txDataBuffer.count(), (size_t)LRTP_MAX_PAYLOAD_SZ);
    // Serial.printf("Consuming %u bytes for next packet...\n", payloadSize);
    if (payloadSize > 0)
    {
        // TODO: Better approach than malloc?
        uint8_t *payloadBuff = (uint8_t *)malloc(sizeof(uint8_t) * payloadSize);

        txPacket->payload = payloadBuff;

        // copy payload to packet struct
        for (int i = 0; i < payloadSize; i++)
        {
            *(payloadBuff++) = *m_txDataBuffer.dequeue();
        }
    }
    // set "static" header fields
    txPacket->payload_length = payloadSize;
    txPacket->version = LRTP_DEFAULT_VERSION;
    txPacket->type = LRTP_DEFAULT_TYPE;
    txPacket->src = m_srcAddr;
    txPacket->dest = m_destAddr;

    unsigned long t_end = millis();
    Serial.printf("Writing packet took: %lu ms for %d bytes\n", t_end - t, payloadSize);
    return txPacket;
}

void LRTPConnection::setTxPacketHeader(LRTPPacket &packet)
{
    // set "dynamic" header fields (i.e. may change between retransmits)
    //  packet.version = LRTP_DEFAULT_VERSION;
    //  packet.type = LRTP_DEFAULT_TYPE;

    // handle flags elsewhere?
    packet.flags.ack = false;
    packet.flags.syn = false;
    packet.flags.fin = false;

    packet.ackWindow = m_windowSize;
    // packet.src = m_srcAddr;
    // packet.dest = m_destAddr;
    packet.seqNum = m_seqNum;
    packet.ackNum = m_ackNum;
}

void LRTPConnection::handleIncomingPacket(const LRTPPacket &packet)
{
    // copy payload into rx buffer
    // TODO: use circular buffer?
    Serial.printf("%s: Packet from: %u, Seq:%u, Ack:%u\n", __PRETTY_FUNCTION__, packet.src, packet.seqNum, packet.ackNum);
    // TODO: Fix this?
    // increment acknowledgement number

    handleIncomingPacketFlags(packet);
    // m_ackNum = packet.seqNum + 1;
    if (packet.payload_length > 0)
    {
        // copy payload
        memcpy(m_rxBuffer, packet.payload, sizeof(uint8_t) * packet.payload_length);
        m_rxBuffLen = packet.payload_length;
        m_rxBuffPos = 0;
    }
}

void LRTPConnection::handleIncomingPacketFlags(const LRTPPacket &packet)
{
    if (packet.flags.ack)
    {
        if (packet.ackNum < m_seqBase || packet.ackNum > m_seqNum - 1)
        {
            Serial.printf("%s: ERROR(?) Packet ACK was out of bounds!\n", __PRETTY_FUNCTION__);
        }
        else
        {
            // uint8_t acknowledgedPackets = packet.ackNum - m_seqBase;
            while (packet.ackNum >= m_seqBase)
            {
                Serial.printf("ACKNOWLEDGED SEQ: %u PACKET\n", m_seqBase);
                LRTPPacket *ackedPacket = m_unackedPackets.dequeue();
                // free the payload buffer
                if (ackedPacket != nullptr)
                    free(ackedPacket->payload);
                m_seqBase++;
            }
            if (m_seqBase == m_seqNum)
            {
                // stop timer
                m_packetTimeoutStarted = false;
            }
            else
            {
                m_packetTimeoutStarted = true;
                m_packetTimeout = millis();
            }
        }
    }
}

uint16_t LRTPConnection::getRemoteAddr()
{
    return m_destAddr;
}