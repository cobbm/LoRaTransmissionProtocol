#include "LRTP.hpp"
// #include "CircularBuffer.hpp"

LRTP::LRTP(uint16_t m_hostAddr) : m_hostAddr(m_hostAddr)
{
}

std::shared_ptr<LRTPConnection> LRTP::connect(uint16_t destAddr)
{
    // check if connection exists
    std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection> >::const_iterator connection = m_activeConnections.find(destAddr);
    if (connection != m_activeConnections.end())
    {
        return connection->second;
    }
    std::shared_ptr<LRTPConnection> newConnection = std::make_shared<LRTPConnection>(m_hostAddr, destAddr);
    m_activeConnections[destAddr] = newConnection;
    return newConnection;
}

int LRTP::begin()
{
    // attach callbacks
    LoRa.onReceive(std::bind(&LRTP::onLoRaPacketReceived, this, std::placeholders::_1));
    LoRa.onTxDone(std::bind(&LRTP::onLoRaTxDone, this));
    LoRa.onCadDone(std::bind(&LRTP::onLoRaCADDone, this));
    LoRa.receive();
    // m_currentLoRaState = LoRaState::IDLE_RECEIVE;
    return 1;
}

void LRTP::onConnect(std::function<void(std::shared_ptr<LRTPConnection>)> callback)
{
    _onConnect = callback;
}

int LRTP::parsePacket(LRTPPacket *outPacket, uint8_t *buf, size_t len)
{
    if (len < LRTP_HEADER_SZ)
    {
        // not enough bytes in packet to parse header!
        return 0;
    }
    uint8_t verAndType = buf[0];
    outPacket->version = verAndType >> 0x04;
    outPacket->type = verAndType & 0x0f;

    uint8_t flagsAndWindow = buf[1];
    // take upper four bits as the flags, parse into struct
    uint8_t rawFlags = flagsAndWindow >> 0x04;
    int flagsParsed = LRTP::parseHeaderFlags(&outPacket->flags, rawFlags);

    // take lower 4 bits for the acknowledgment window
    outPacket->ackWindow = flagsAndWindow & 0x0f;

    // read source node address (16 bit big-endian int) from bytes 2-3
    uint16_t srcNode = (buf[2] << 0x08) | (buf[3]);
    outPacket->src = srcNode;
    // read destination node address (16 bit big-endian int) from bytes 4-5
    uint16_t destNode = (buf[4] << 0x08) | (buf[5]);
    outPacket->dest = destNode;
    outPacket->seqNum = buf[6];
    outPacket->ackNum = buf[7];
    // set payload pointer to the start of the payload and compute payload length
    outPacket->payload = buf + LRTP_HEADER_SZ;
    outPacket->payload_length = len - LRTP_HEADER_SZ;
    return 1;
}

void LRTP::handleIncomingPacket(const LRTPPacket &packet)
{
    Serial.printf("%s: Handling packet:\n", __PRETTY_FUNCTION__);
    debug_print_packet(packet);

    // find connection pertaining to this packet
    std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection> >::const_iterator connection = m_activeConnections.find(packet.src);
    if (connection == m_activeConnections.end())
    {
        // the source of the packet is not in our active connections!
        // it may be a new incoming connection, otherwise we should ignore it
        return handleIncomingConnectionPacket(packet);
    }
    // pass the packet onto the connection:
    return connection->second->handleIncomingPacket(packet);
}

void LRTP::handleIncomingConnectionPacket(const LRTPPacket &packet)
{
    Serial.printf("%s: Handling Connection packet:\n", __PRETTY_FUNCTION__);
    // TODO: check for syn flag and respond with ACK
    std::shared_ptr<LRTPConnection> newConnection = std::make_shared<LRTPConnection>(m_hostAddr, packet.src);
    m_activeConnections[packet.src] = newConnection;

    if (_onConnect != nullptr)
        _onConnect(newConnection);

    newConnection->handleIncomingPacket(packet);
}

int LRTP::parseHeaderFlags(LRTPFlags *outFlags, uint8_t rawFlags)
{
    outFlags->syn = (rawFlags >> 0x03) & 0x01;
    outFlags->fin = (rawFlags >> 0x02) & 0x01;
    outFlags->ack = (rawFlags >> 0x01) & 0x01;
    return 1;
}

uint8_t LRTP::packFlags(const LRTPFlags &flags)
{
    return (flags.syn << 0x03) | (flags.fin << 0x02) | (flags.ack << 0x01);
}

void LRTP::loop()
{
    loopReceive();
    loopTransmit();
}

void LRTP::loopReceive()
{
    if (m_loraRxBytesWaiting > 0)
    {
        LRTPPacket pkt;
        int parseResult = LRTP::parsePacket(&pkt, this->m_rxBuffer, m_loraRxBytesWaiting);
        if (parseResult)
        {
            handleIncomingPacket(pkt);
        }
        m_loraRxBytesWaiting = 0;
    }
}

void LRTP::loopTransmit()
{
    if (m_activeConnections.size() > 0)
    {
        // loop round-robin through the current connections, until a new packet is found for transmit
        static std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection> >::const_iterator txTarget = m_activeConnections.begin();
        // bool gotPacket = false;
        LRTPPacket *txPacket = nullptr;
        while (txTarget != m_activeConnections.end())
        {
            txPacket = txTarget->second->getNextTxPacket();
            if (txPacket != nullptr)
                break;
            ++txTarget;
        };
        if (txPacket != nullptr)
            sendPacket(*txPacket);
        // loop back to the beginning if we've reached the end of the map
        if (txTarget == m_activeConnections.end())
            txTarget = m_activeConnections.begin();
    }
}

void LRTP::sendPacket(const LRTPPacket &packet)
{
    Serial.printf("%s: Sending Packet of length: %d. Src: %d, Dest: %u\n", __PRETTY_FUNCTION__, packet.payload_length, packet.src, packet.dest);

    uint8_t verAndType = (packet.version << 0x04) | (packet.type & 0x0f);
    uint8_t flagsAndWindow = (packFlags(packet.flags) << 0x04) | (packet.ackWindow & 0x0f);
    uint8_t src_hi = packet.src >> 0x08;
    uint8_t src_lo = packet.src & 0xff;
    uint8_t dest_hi = packet.dest >> 0x08;
    uint8_t dest_lo = packet.dest & 0xff;
    // write packet data
    LoRa.beginPacket();
    LoRa.write(verAndType);
    LoRa.write(flagsAndWindow);
    LoRa.write(src_hi);
    LoRa.write(src_lo);
    LoRa.write(dest_hi);
    LoRa.write(dest_lo);
    LoRa.write(packet.seqNum);
    LoRa.write(packet.ackNum);
    LoRa.write(packet.payload, packet.payload_length);
    LoRa.endPacket();
}

// handlers for LoRa async
void LRTP::onLoRaPacketReceived(int packetSize)
{
    Serial.printf("%s: Received Packet of length: %d! My addr is %d\n", __PRETTY_FUNCTION__, packetSize, this->m_hostAddr);
    // read packet into buffer
    uint8_t *bufferStart = this->m_rxBuffer;
    // size_t rxMax = (LRTP_MAX_PACKET * LRTP_GLOBAL_RX_BUFFER_SZ) - 1;
    while (LoRa.available())
    {
        *(bufferStart++) = (uint8_t)LoRa.read();
    }
    m_loraRxBytesWaiting = packetSize;
}
void LRTP::onLoRaTxDone()
{
    Serial.printf("%s: TX Done\n", __PRETTY_FUNCTION__);
}
void LRTP::onLoRaCADDone()
{
    Serial.printf("%s: CAD Done\n", __PRETTY_FUNCTION__);
}

void debug_print_packet(const LRTPPacket &packet)
{
    Serial.printf("Version: %u (0x%02X)\n", packet.version, packet.version);
    Serial.printf("Type: %u (0x%02X)\n", packet.type, packet.type);
    Serial.printf("Flags: (0x%02X) ", LRTP::packFlags(packet.flags));
    if (packet.flags.syn)
    {
        Serial.print("S ");
    }
    else
    {
        Serial.print("- ");
    }
    if (packet.flags.fin)
    {
        Serial.print("F ");
    }
    else
    {
        Serial.print("- ");
    }
    if (packet.flags.ack)
    {
        Serial.print("A ");
    }
    else
    {
        Serial.print("- ");
    }
    Serial.printf("X\n");
    Serial.printf("Ack Window: %u (0x%02X)\n", packet.ackWindow, packet.ackWindow);
    Serial.printf("Source: %u (0x%04X)\n", packet.src, packet.src);
    Serial.printf("Destination: %u (0x%04X)\n", packet.dest, packet.dest);
    Serial.printf("Sequence Num: %u (0x%02X)\n", packet.seqNum, packet.seqNum);
    Serial.printf("Acknowledgment Num: %u (0x%02X)\n", packet.ackNum, packet.ackNum);
    Serial.printf("Payload: (%u bytes)\n", packet.payload_length);
    for (int i = 0; i < packet.payload_length; i++)
    {
        Serial.print(packet.payload[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    for (int i = 0; i < packet.payload_length; i++)
    {
        Serial.print((char)packet.payload[i]);
    }
}