#include "LRTP.hpp"

LRTP::LRTP(uint16_t m_hostAddr) : m_hostAddr(m_hostAddr), m_currentLoRaState(LoRaState::IDLE_RECEIVE)
{
}

std::shared_ptr<LRTPConnection> LRTP::connect(uint16_t destAddr)
{
    // check if connection exists
    std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection>>::const_iterator connection = m_activeConnections.find(destAddr);
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
    LoRa.onCadDone(std::bind(&LRTP::onLoRaCADDone, this, std::placeholders::_1));
    LoRa.receive();
    m_currentLoRaState = LoRaState::IDLE_RECEIVE;
    return 1;
}

// void LRTP::end(){
// TODO
// }

// callback handlers
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
    outPacket->payloadType = verAndType & 0x0f;

    uint8_t flagsAndWindow = buf[1];
    // take upper four bits as the flags, parse into struct
    uint8_t rawFlags = flagsAndWindow >> 0x04;
    int flagsParsed = LRTP::parseHeaderFlags(&outPacket->flags, rawFlags);

    // take lower 4 bits for the acknowledgment window
    outPacket->ackWindow = flagsAndWindow & 0x0f;

    // read source node address (16 bit big-endian) from bytes 2-3
    uint16_t srcAddr_no = (buf[2]) | (buf[3] << 0x08);

    // read destination node address (16 bit big-endian) from bytes 4-5
    uint16_t destAddr_no = (buf[4]) | (buf[5] << 0x08);
    // convert addresses from network order to host order:
    outPacket->src = ntohs(srcAddr_no);
    outPacket->dest = ntohs(destAddr_no);
    outPacket->seqNum = buf[6];
    outPacket->ackNum = buf[7];
    // set payload pointer to the start of the payload and compute payload length
    outPacket->payload = buf + LRTP_HEADER_SZ;
    outPacket->payloadLength = len - LRTP_HEADER_SZ;
    return 1;
}

void LRTP::handleIncomingPacket(const LRTPPacket &packet)
{
// Serial.printf("%s: Handling packet:\n", __PRETTY_FUNCTION__);
// debug_print_packet(packet);
#if LRTP_DEBUG > 0
    Serial.printf("%s: Handling packet from: %u, Seq:%u, Ack:%u\n", __PRETTY_FUNCTION__, packet.src, packet.seqNum, packet.ackNum);
#endif

    // find connection pertaining to this packet
    std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection>>::const_iterator connection = m_activeConnections.find(packet.src);
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
    if (packet.flags.syn && packet.payloadLength == 0)
    {
        std::shared_ptr<LRTPConnection> newConnection = std::make_shared<LRTPConnection>(m_hostAddr, packet.src);
        m_activeConnections[packet.src] = newConnection;

        newConnection->handleIncomingPacket(packet);
        if (_onConnect != nullptr)
            _onConnect(newConnection);
    }
    else
    {
        Serial.printf("%s: Error: invalid packet received - SYN flag not present or payload included!\n", __PRETTY_FUNCTION__);
    }
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
            if (pkt.dest == m_hostAddr)
            {
                handleIncomingPacket(pkt);
            }
            else if (pkt.dest == LRTP_BROADCAST_ADDR)
            {
                // handleIncomingBroadcastPacket(pkt)
                Serial.printf("%s: Broadcast packet received! TODO Implement broadcast!\n", __PRETTY_FUNCTION__);
            }
            else
            {
                Serial.printf("%s: Packet src: %u, dest: %u, was not addressed to me - ignored\n", __PRETTY_FUNCTION__, pkt.src, pkt.dest);
            }
        }
        else
        {
            Serial.printf("%s: ERROR: Could not parse packet!\n", __PRETTY_FUNCTION__);
        }
        m_loraRxBytesWaiting = 0;
        setState(LoRaState::IDLE_RECEIVE);
    }
}

void LRTP::loopTransmit()
{
    unsigned long t = millis();
    // if (m_loraRxBytesWaiting > 0)
    //     return;
    //  if radio is currently idle, get the next packet to send, if it exists
    if (m_currentLoRaState == LoRaState::IDLE_RECEIVE)
    {
        if (m_activeConnections.size() > 0)
        {
            // loop round-robin through the current connections, until we find a connection which has a packet ready for transmit
            static std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection>>::const_iterator txTarget = m_activeConnections.begin();

            while (txTarget != m_activeConnections.end())
            {
                // check if the connection has closed
                if (txTarget->second->getConnectionState() == LRTPConnState::CLOSED)
                {
                    Serial.printf("%s: Warning: Connection has closed\n", __PRETTY_FUNCTION__);
                }
                // update each connection
                txTarget->second->updateTimers(t);
                if (txTarget->second->isReadyForTransmit())
                {
                    m_nextConnectionForTransmit = txTarget->second.get();
                    beginCAD();
                    break;
                }
                // move iterator forward to the next conenction in the map
                ++txTarget;
            }
            // loop back to the beginning if we reach the end of the map
            if (txTarget == m_activeConnections.end())
                txTarget = m_activeConnections.begin();
        }
    }
    else if (m_currentLoRaState == LoRaState::CAD_FINISHED)
    {
        handleCADDone(t);
    }
    else if (m_currentLoRaState == LoRaState::RECEIVE)
    {
        handleReceiveTimeout(t);
    }
}

bool LRTP::beginCAD()
{
    // check if the radio is receiving a packet
    bool channelFree = !LoRa.rxSignalDetected();
    if (channelFree)
    {
        setState(LoRaState::CAD_STARTED);
        // set CAD counter
        m_cadRoundsRemaining = LRTP_CAD_ROUNDS;
// put the radio into CAD mode only if we're not mid-way through receiveing a packet
#if LRTP_DEBUG > 1
        Serial.println("\nbeginCAD(): Start");
#endif
        LoRa.channelActivityDetection();
    }
    else
    {
        m_checkReceiveRounds = LORA_SIGNAL_TIMEOUT_ROUNDS;
        setState(LoRaState::RECEIVE);
#if LRTP_DEBUG > 0
        Serial.println("\nbeginCAD(): Start - rxSignalDetected!");
#endif
    }
    return channelFree;
}

void LRTP::sendPacket(const LRTPPacket &packet)
{
#if LRTP_DEBUG > 1
    Serial.printf("%s: Sending Packet. length: %d. Src: %d, Dest: %u Seq: %u, Ack: %u\n", __PRETTY_FUNCTION__, packet.payloadLength, packet.src, packet.dest, packet.seqNum, packet.ackNum);
#endif
    setState(LoRaState::TRANSMIT);

    uint8_t verAndType = (packet.version << 0x04) | (packet.payloadType & 0x0f);
    uint8_t flagsAndWindow = (packFlags(packet.flags) << 0x04) | (packet.ackWindow & 0x0f);
    // convert src and dest addresses from host to network order (big-endian)
    uint16_t src_no = htons(packet.src);
    uint16_t dest_no = htons(packet.dest);
    // now take higher and lower bytes of the 16 bit addresses
    uint8_t src_lo = src_no >> 0x08;
    uint8_t src_hi = src_no & 0xff;
    uint8_t dest_lo = dest_no >> 0x08;
    uint8_t dest_hi = dest_no & 0xff;
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
    LoRa.write(packet.payload, packet.payloadLength);
    LoRa.endPacket(true);
}

// handlers for LoRa async
void LRTP::onLoRaPacketReceived(int packetSize)
{
#if LRTP_DEBUG > 1
    Serial.printf("%s: Received Packet of length: %d!\n", __PRETTY_FUNCTION__, packetSize);
#endif
    // read packet into buffer
    uint8_t *bufferStart = this->m_rxBuffer;
    // size_t rxMax = (LRTP_MAX_PACKET * LRTP_GLOBAL_RX_BUFFER_SZ) - 1;
    while (LoRa.available() > 0)
    {
        *(bufferStart++) = (uint8_t)LoRa.read();
    }
    m_loraRxBytesWaiting = packetSize;
    // set state back to idle/receive
    // m_currentLoRaState = LoRaState::IDLE_RECEIVE;
}

void LRTP::onLoRaTxDone()
{
#if LRTP_DEBUG > 0
    Serial.printf("%s: TX Done\n", __PRETTY_FUNCTION__);
#endif
    setState(LoRaState::IDLE_RECEIVE);
    // put radio back into receive mode
    LoRa.receive();
}

void LRTP::onLoRaCADDone(bool channelBusy)
{
#if LRTP_DEBUG > 1
    Serial.printf("CAD %s (%u) ", channelBusy ? "BUSY" : "", m_cadRoundsRemaining);
#endif
    m_channelActive = channelBusy;
    if (channelBusy)
    {
        // finish early if channel is busy and enter receive mode to receive the incoming packet
        m_checkReceiveRounds = LORA_SIGNAL_TIMEOUT_ROUNDS;
        setState(LoRaState::RECEIVE);
        LoRa.receive();
#if LRTP_DEBUG > 0
        Serial.printf("\nCAD (%u/%u) interrupted!\n", LRTP_CAD_ROUNDS - m_cadRoundsRemaining, LRTP_CAD_ROUNDS);
#endif
        return;
    }
    if (m_cadRoundsRemaining > 1)
    {
        m_cadRoundsRemaining--;
        // start channel activity detect again
        LoRa.channelActivityDetection();
    }
    else
    {
        setState(LoRaState::CAD_FINISHED);
#if LRTP_DEBUG > 1
        Serial.println("CAD Finished");
#endif
    }
}

void LRTP::handleReceiveTimeout(unsigned long t)
{
    // fix to prevent getting stuck in RECEIVE state if a corrupt/partial packet is received and onPacketReceive callback is never called
    if (t - m_timer_checkReceiveTimeout >= LORA_SIGNAL_TIMEOUT)
    {
        bool receiving = LoRa.rxSignalDetected();
        if (receiving)
        {
            m_checkReceiveRounds = LORA_SIGNAL_TIMEOUT_ROUNDS;
        }
        else if (m_checkReceiveRounds <= 1)
        {
            // no signal has been detected, switch back to idle state
            setState(LoRaState::IDLE_RECEIVE);
        }
        else
        {
            m_checkReceiveRounds--;
        }
        m_timer_checkReceiveTimeout = t;
    }
}

void LRTP::handleCADDone(unsigned long t)
{
// transmit after CAD finishes
#if LRTP_DEBUG > 1
    Serial.printf("CAD finished: Busy: %u.\n", m_channelActive);
#endif
    LRTPPacket *p = m_nextConnectionForTransmit->getNextTxPacket(t);
    if (p != nullptr)
    {
#if LRTP_DEBUG > 3
        debug_print_packet(*p);
#endif
        sendPacket(*p);
    }
    else
    {
        Serial.printf("%s: ERROR: Transmit packet was null!\n", __PRETTY_FUNCTION__);
        setState(LoRaState::IDLE_RECEIVE);
    }
}

void LRTP::setState(LoRaState newState)
{
#if LRTP_DEBUG > 0
    Serial.printf("%s: LORA Radio Change State: %s -> %s\n", __PRETTY_FUNCTION__, LORAStateToStr(m_currentLoRaState), LORAStateToStr(newState));
#endif
    m_currentLoRaState = newState;
}

// ======= DEBUG METHODS =======
void debug_print_packet(const LRTPPacket &packet)
{
    Serial.printf("Version: %u (0x%02X)\n", packet.version, packet.version);
    Serial.printf("Type: %u (0x%02X)\n", packet.payloadType, packet.payloadType);
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
    Serial.printf("Payload: (%u bytes)\n", packet.payloadLength);
    for (int i = 0; i < packet.payloadLength; i++)
    {
        Serial.print(packet.payload[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    for (int i = 0; i < packet.payloadLength; i++)
    {
        Serial.print((char)packet.payload[i]);
    }
    Serial.println();
}

const char *LORAStateToStr(LoRaState state)
{
    switch (state)
    {
    case LoRaState::IDLE_RECEIVE:
        return "IDLE_RECEIVE";
        break;
    case LoRaState::RECEIVE:
        return "RECEIVE";
        break;
    case LoRaState::CAD_STARTED:
        return "CAD_STARTED";
        break;
    case LoRaState::CAD_FINISHED:
        return "CAD_FINISHED";
        break;
    case LoRaState::TRANSMIT:
        return "TRANSMIT";
        break;
    default:
        return "Invalid State";
        break;
    }
}