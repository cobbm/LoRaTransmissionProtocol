#include "LRTP.hpp"

LRTP::LRTP(uint16_t hostAddr) : hostAddr(hostAddr)
{
}

int LRTP::begin()
{
    // attach callbacks
    LoRa.onReceive(std::bind(&LRTP::onLoRaPacketReceived, this, std::placeholders::_1));
    LoRa.onTxDone(std::bind(&LRTP::onLoRaTxDone, this));
    LoRa.onCadDone(std::bind(&LRTP::onLoRaCADDone, this));
    LoRa.receive();

    return 1;
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
    if (loraRxBytesWaiting > 0)
    {
        LRTPPacket pkt;
        int parseResult = LRTP::parsePacket(&pkt, this->m_rxBuffer, loraRxBytesWaiting);
        if (parseResult)
        {
            handleIncomingPacket(pkt);
        }
        loraRxBytesWaiting = 0;
    }
}

// handlers for LoRa async
void LRTP::onLoRaPacketReceived(int packetSize)
{
    Serial.printf("%s: Received Packet of length: %d! My addr is %d\n", __PRETTY_FUNCTION__, packetSize, this->hostAddr);
    // read packet into buffer
    uint8_t *bufferStart = this->m_rxBuffer;
    // size_t rxMax = (LRTP_MAX_PACKET * LRTP_GLOBAL_RX_BUFFER_SZ) - 1;
    while (LoRa.available())
    {
        *(bufferStart++) = (uint8_t)LoRa.read();
    }
    loraRxBytesWaiting = packetSize;
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