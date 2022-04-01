#include "LRTPConnection.hpp"

LRTPConnection::LRTPConnection(uint16_t destination) : m_destAddr(destination), m_txBuffer(LRTP_TX_BUFFER_SZ), m_state(LRTPConnState::CLOSED)
{
}

// Stream implementation
int LRTPConnection::read()
{
    return 0;
}
int LRTPConnection::available()
{
    return 0;
}
int LRTPConnection::peek()
{
    return 0;
}

// Print implementation
size_t LRTPConnection::write(uint8_t val)
{
    Serial.printf("Stream wrote: %c\n", val);
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