#include "LRTPConnection.hpp"

// #include "CircularBuffer.hpp"

LRTPConnection::LRTPConnection(uint16_t source, uint16_t destination) : m_srcAddr(source), m_destAddr(destination), m_txDataBuffer(LRTP_MAX_PAYLOAD_SZ * LRTP_TX_BUFFER_SZ), m_txPacketBuff(LRTP_TX_BUFFER_SZ), m_state(LRTPConnState::CLOSED)
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

// Print implementation
size_t LRTPConnection::write(uint8_t val)
{
    Serial.printf("Stream wrote: %c\n", val);
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

LRTPPacket *LRTPConnection::getNextTxPacket()
{
    // check if any packets need to be resent
    // send the next packet
    return prepareNextTxPacket();
}

LRTPPacket *LRTPConnection::prepareNextTxPacket()
{
    // return the next packet available for transmitting
    // try and get a new packet to buffer
    if (m_txDataBuffer.count() <= 0)
    {
        // no data waiting
        return nullptr;
    }

    LRTPPacket *txPacket = m_txPacketBuff.enqueueEmpty();
    if (txPacket == nullptr)
    {
        Serial.printf("%s: Connection %u: TX packet buffer full\n", __PRETTY_FUNCTION__, m_destAddr);
        // allocation failed, tx buffer is full of unacknowledged packets
        return nullptr;
    }

    // construct the packet with as much data as we can take
    size_t payloadSize = min(m_txDataBuffer.count(), (size_t)LRTP_MAX_PAYLOAD_SZ);
    txPacket->version = LRTP_DEFAULT_VERSION;
    txPacket->type = LRTP_DEFAULT_TYPE;

    // handle flags elsewhere?
    txPacket->flags.ack = false;
    txPacket->flags.syn = false;
    txPacket->flags.fin = false;

    txPacket->ackWindow = LRTP_DEFAULT_ACKWIN;
    txPacket->src = m_srcAddr;
    txPacket->dest = m_destAddr;
    txPacket->seqNum = m_seqNum++;
    txPacket->ackNum = m_ackNum;

    // TODO: Better approach than malloc?
    uint8_t *payloadBuff = (uint8_t *)malloc(sizeof(uint8_t) * payloadSize);

    txPacket->payload = payloadBuff;
    txPacket->payload_length = payloadSize;
    // copy payload to packet struct
    for (int i = 0; i < payloadSize; i++)
    {
        *(payloadBuff++) = *m_txDataBuffer.dequeue();
    }
    return txPacket;
}

void LRTPConnection::handleIncomingPacket(const LRTPPacket &packet)
{
    // copy payload into rx buffer
    // TODO: use circular buffer?
    Serial.printf("%s: Packet from: %u, S:%u, A:%u\n", __PRETTY_FUNCTION__, packet.src, packet.seqNum, packet.ackNum);
    // TODO: Fix this?
    // increment acknowledgement number
    m_ackNum = packet.seqNum + 1;
    // copy payload
    memcpy(m_rxBuffer, packet.payload, sizeof(uint8_t) * packet.payload_length);
    m_rxBuffLen = packet.payload_length;
    m_rxBuffPos = 0;
}

uint16_t LRTPConnection::getRemoteAddr()
{
    return m_destAddr;
}