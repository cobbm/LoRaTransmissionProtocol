#include "LRTP.h"

LRTP::LRTP(uint16_t m_hostAddr) : m_hostAddr(m_hostAddr), m_currentLoRaState(LoRaState::IDLE_RECEIVE) {
}

std::shared_ptr<LRTPConnection> LRTP::connect(uint16_t destAddr) {
    std::shared_ptr<LRTPConnection> connection = nullptr;
    // check if connection exists
    std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection>>::const_iterator connection_iter = m_activeConnections.find(destAddr);
    if (connection_iter != m_activeConnections.end()) {
        connection = connection_iter->second;
    } else {
        connection = std::make_shared<LRTPConnection>(m_hostAddr, destAddr);
        m_activeConnections[destAddr] = connection;
        if (connection->getConnectionState() == LRTPConnState::CLOSED)
            connection->connect();
    }
    return connection;
}

int LRTP::begin() {
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
void LRTP::onConnect(std::function<void(std::shared_ptr<LRTPConnection>)> callback) {
    _onConnect = callback;
}

void LRTP::onBroadcastPacket(std::function<void(const LRTPPacket &)> callback) {
    _onBroadcastPacket = callback;
}

int LRTP::parsePacket(LRTPPacket *outPacket, uint8_t *buf, size_t len) {
    if (len < LRTP_HEADER_SZ) {
        // not enough bytes in packet to parse header!
        lrtp_debug("not enough bytes in packet to parse header");
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

void LRTP::handleIncomingPacket(const LRTPPacket &packet) {
    debug_print_packet(packet);

    // find connection pertaining to this packet
    std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection>>::const_iterator connection = m_activeConnections.find(packet.src);
    if (connection == m_activeConnections.end()) {
        // the source of the packet is not in our active connections!
        // it may be a new incoming connection, otherwise we should ignore it
        lrtp_debug("Source of packet not in active connections");
        return handleIncomingConnectionPacket(packet);
    }
    // pass the packet on to the connection:
    return connection->second->handleIncomingPacket(packet);
}

void LRTP::handleIncomingConnectionPacket(const LRTPPacket &packet) {
    // lrtp_debugf("Handling Connection packet:\n");

    if (packet.flags.syn && packet.payloadLength == 0) {
        std::shared_ptr<LRTPConnection> newConnection = std::make_shared<LRTPConnection>(m_hostAddr, packet.src);

        m_activeConnections[packet.src] = newConnection;

        newConnection->handleIncomingPacket(packet);
        if (_onConnect != nullptr)
            _onConnect(newConnection);
    } else {
        lrtp_debug("Error: invalid packet received - SYN flag not present or payload included!\n");
    }
}

void LRTP::handleIncomingBroadcastPacket(const LRTPPacket &packet) {
    if (_onBroadcastPacket != nullptr)
        _onBroadcastPacket(packet);
}

int LRTP::parseHeaderFlags(LRTPFlags *outFlags, uint8_t rawFlags) {
    outFlags->syn = (rawFlags >> 0x03) & 0x01;
    outFlags->fin = (rawFlags >> 0x02) & 0x01;
    outFlags->ack = (rawFlags >> 0x01) & 0x01;
    return 1;
}

uint8_t LRTP::packFlags(const LRTPFlags &flags) {
    return (flags.syn << 0x03) | (flags.fin << 0x02) | (flags.ack << 0x01);
}

void LRTP::loop() {

    loopReceive();
    loopTransmit();
}

void LRTP::loopReceive() {
    // lrtp_debugf("Bytes Waiting: %u\n", m_loraRxBytesWaiting);

    if (m_loraRxBytesWaiting > 0) {

        lrtp_debugf("LORA: Bytes Waiting: %u\n", m_loraRxBytesWaiting);
        LRTPPacket pkt;
        int parseResult = LRTP::parsePacket(&pkt, this->m_rxBuffer, m_loraRxBytesWaiting);
        if (parseResult) {
            if (pkt.dest == m_hostAddr) {
                handleIncomingPacket(pkt);
            } else if (pkt.dest == LRTP_BROADCAST_ADDR) {
                handleIncomingBroadcastPacket(pkt);

                lrtp_debug("Broadcast packet received! TODO Implement broadcast!\n");

            } else {
                lrtp_debugf("Packet src: %u, dest: %u, was not addressed to me - ignored "
                            "TODO: implement dump of entire LoRa packet\n",
                    pkt.src,
                    pkt.dest);
            }
        } else {
            lrtp_debug("ERROR: Could not parse packet!");
        }
        m_loraRxBytesWaiting = 0;
        setState(LoRaState::IDLE_RECEIVE);
    }
}

void LRTP::loopTransmit() {

    // debug("LORA loopTransmit()");
    unsigned long t = millis();

    // TODO: Investigate:
    //// Step 1: Process transmissions
    //
    // auto oldState = m_currentLoRaState;
    // auto newState = processXmit();
    //
    // if (oldState != newState)
    // {
    //      // Step 2:handle state chage caused by processXmit();
    //      if (newState == LoRaState::IDLE_RECEIVE) {
    //          // We are now in IDLE_RECEIVE state
    //      }
    //      else if (newState == LoRaState::RECEIVE)
    //      {
    //          // We just entered RECEIVE state
    //      }
    //      // etc...
    //
    //      m_currentLoRaState = newState; // VERY IMPORTANT
    // }

    // if (m_loraRxBytesWaiting > 0)
    //     return;

    //  if radio is currently idle, get the next packet to send, if it exists
    if (m_currentLoRaState == LoRaState::IDLE_RECEIVE) {
        if (m_activeConnections.size() > 0) {
            // loop round-robin through the current connections, until we find a
            // connection which has a packet ready for transmit
            static std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection>>::const_iterator txTarget = m_activeConnections.begin();

            while (txTarget != m_activeConnections.end()) {
                if (!txTarget->second) {

                    lrtp_debug("NULL POINTER!");
                    continue;
                }
                // check if the connection has closed
                if (txTarget->second->getConnectionState() == LRTPConnState::CLOSED) {
                    lrtp_debug("Warning: Connection has closed");
                }
                // print any IRQ errors
                if (txTarget->second->m_connectionError != LRTPError::NONE) {
                    lrtp_debugf("IRQ ERROR: code %u ! (previous errors that occured since the "
                                "last check may have been missed)\n",
                        (int)txTarget->second->m_connectionError);
                    txTarget->second->m_connectionError = LRTPError::NONE;
                }
                // update each connection
                txTarget->second->updateTimers(t);

                if (txTarget->second->isReadyForTransmit()) {
                    lrtp_info("Ready for transmit. Starting CAD");
                    m_nextConnectionForTransmit = txTarget->second;
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
    } else if (m_currentLoRaState == LoRaState::CAD_FINISHED) {
        handleCADDone(t);
    } else if (m_currentLoRaState == LoRaState::RECEIVE) {
        handleReceiveTimeout(t);
    }
}

bool LRTP::beginCAD() {

    lrtp_info("beginCAD");

    // check if the radio is receiving a packet
    bool channelFree = !LoRa.rxSignalDetected();
    if (channelFree) {
        setState(LoRaState::CAD_STARTED);
        // set CAD counter
        m_cadRoundsRemaining = LRTP_CAD_ROUNDS;
        // put the radio into CAD mode only if we're not mid-way through receiveing
        // a packet
        lrtp_debug("beginCAD - Channel Free");

        LoRa.channelActivityDetection();
    } else {
        m_checkReceiveRounds = LORA_SIGNAL_TIMEOUT_ROUNDS;
        setState(LoRaState::RECEIVE);

        lrtp_debugf("beginCAD- rxSignalDetected! Receive rounds %u", LORA_SIGNAL_TIMEOUT_ROUNDS);
    }
    return channelFree;
}

std::vector<uint8_t> LRTP::preparePacket(const LRTPPacket &packet) {

#ifdef LRTP_DEBUG
    // lrtp_debugf("Sending Packet. length: %d. Src: %d, Dest: %u Seq: %u, Ack:
    // %u\n",
    //        packet.payloadLength, packet.src, packet.dest, packet.seqNum,
    //        packet.ackNum);
#endif

    char flagsStr[] = { packet.flags.syn ? 'S' : '-', packet.flags.fin ? 'F' : '-', packet.flags.ack ? 'A' : '-', 0 };
    lrtp_infof("PREPARE PACKET: length: %d, src: %d, dest: %d, flags: %s, %u seq: %u, ack: %u\n",
        packet.payloadLength,
        packet.src,
        packet.dest,
        flagsStr,
        packet.seqNum,
        packet.ackNum);

    setState(LoRaState::TRANSMIT);

    // pack the version and type into a single byte. shift version left by 4 bits
    // and OR with the lower 4 bits of the payload type
    uint8_t verAndType = (packet.version << 0x04) | (packet.payloadType & 0x0f);
    // pack the flags and acknowledgment window into a single byte. shift flag
    // bits left by 4 bits and OR with the lower 4 bits of the acknowledgment
    // window size
    uint8_t flagsAndWindow = (packFlags(packet.flags) << 0x04) | (packet.ackWindow & 0x0f);
    // convert src and dest addresses from host to network order (big-endian)
    uint16_t src_addr = htons(packet.src);
    uint16_t dest_addr = htons(packet.dest);
    // now take higher and lower bytes of the 16 bit addresses
    uint8_t src_lo = src_addr >> 0x08;
    uint8_t src_hi = src_addr & 0xff;
    uint8_t dest_lo = dest_addr >> 0x08;
    uint8_t dest_hi = dest_addr & 0xff;

    /*
    // write packet data in order specified by the specification
    LoRa.beginPacket();
    LoRa.write(verAndType);
    LoRa.write(flagsAndWindow);
    LoRa.write(src_hi);
    LoRa.write(src_lo);
    LoRa.write(dest_hi);
    LoRa.write(dest_lo);
    LoRa.write(packet.seqNum);
    LoRa.write(packet.ackNum);
    // write the actual payload:
    LoRa.write(packet.payload, packet.payloadLength);
    // call endPacket with true to use async mode
    LoRa.endPacket(true);
    */
    std::vector<uint8_t> data(LRTP_HEADER_SZ + packet.payloadLength);

    data[0] = verAndType;
    data[1] = flagsAndWindow;
    data[2] = src_hi;
    data[3] = src_lo;
    data[4] = dest_hi;
    data[5] = dest_lo;
    data[6] = packet.seqNum;
    data[7] = packet.ackNum;

    // TODO: write payload too
    throw std::runtime_error("Unimplemented!");

    return data;
}

void LRTP::sendPacket(const LRTPPacket &packet) {
    char flagsStr[] = { packet.flags.syn ? 'S' : '-', packet.flags.fin ? 'F' : '-', packet.flags.ack ? 'A' : '-', 0 };

    lrtp_infof("Sending Packet. length: %d, src: %d, dest: %u, flags: %s, seq: %u, ack: %u\n",
        packet.payloadLength,
        packet.src,
        packet.dest,
        flagsStr,
        packet.seqNum,
        packet.ackNum);

    setState(LoRaState::TRANSMIT);

    // pack the version and type into a single byte. shift version left by 4 bits
    // and OR with the lower 4 bits of the payload type
    uint8_t verAndType = (packet.version << 0x04) | (packet.payloadType & 0x0f);
    // pack the flags and acknowledgment window into a single byte. shift flag
    // bits left by 4 bits and OR with the lower 4 bits of the acknowledgment
    // window size
    uint8_t flagsAndWindow = (packFlags(packet.flags) << 0x04) | (packet.ackWindow & 0x0f);
    // convert src and dest addresses from host to network order (big-endian)
    uint16_t src_addr = htons(packet.src);
    uint16_t dest_addr = htons(packet.dest);
    // now take higher and lower bytes of the 16 bit addresses
    uint8_t src_lo = src_addr >> 0x08;
    uint8_t src_hi = src_addr & 0xff;
    uint8_t dest_lo = dest_addr >> 0x08;
    uint8_t dest_hi = dest_addr & 0xff;
    // write packet data in order specified by the specification
    LoRa.beginPacket();
    LoRa.write(verAndType);
    LoRa.write(flagsAndWindow);
    LoRa.write(src_hi);
    LoRa.write(src_lo);
    LoRa.write(dest_hi);
    LoRa.write(dest_lo);
    LoRa.write(packet.seqNum);
    LoRa.write(packet.ackNum);
    // write the actual payload:
    LoRa.write(packet.payload, packet.payloadLength);
    // call endPacket with true to use async mode
    LoRa.endPacket(true);
}

// handlers for LoRa async
// ISR!
void LRTP::onLoRaPacketReceived(int packetSize) {

    // lrtp_debugf("Received Packet of length: %d!\n", packetSize);

    // read packet into buffer
    uint8_t *bufferStart = this->m_rxBuffer;
    // size_t rxMax = (LRTP_MAX_PACKET * LRTP_GLOBAL_RX_BUFFER_SZ) - 1;
    while (LoRa.available() > 0) {
        *(bufferStart++) = (uint8_t)LoRa.read();
    }
    m_loraRxBytesWaiting = packetSize;
    // set state back to idle/receive
    // m_currentLoRaState = LoRaState::IDLE_RECEIVE;
}

void LRTP::onLoRaTxDone() {

    // debug("TX Done");

    setState(LoRaState::IDLE_RECEIVE);
    // put radio back into receive mode
    LoRa.receive();
}

void LRTP::onLoRaCADDone(bool channelBusy) {

    /*
  debugf("CAD %s (%u of ) \n", channelBusy ? "BUSY" : "FREE",
         m_cadRoundsRemaining, LORA_SIGNAL_TIMEOUT_ROUNDS);
         */
    lrtp_debugf("CAD %s (%u of %u) ", channelBusy ? "[BUSY]" : "[FREE]", m_cadRoundsRemaining, LORA_SIGNAL_TIMEOUT_ROUNDS);

    m_channelActive = channelBusy;
    if (channelBusy) {
        // finish early if channel is busy and enter receive mode to receive the
        // incoming packet
        m_checkReceiveRounds = LORA_SIGNAL_TIMEOUT_ROUNDS;
        setState(LoRaState::RECEIVE);
        LoRa.receive();

        lrtp_debugf("CAD (%u/%u) interrupted!\n", LRTP_CAD_ROUNDS - m_cadRoundsRemaining, LRTP_CAD_ROUNDS);

        return;
    }
    if (m_cadRoundsRemaining > 1) {
        m_cadRoundsRemaining--;
        // start channel activity detect again
        LoRa.channelActivityDetection();
    } else {
        setState(LoRaState::CAD_FINISHED);

        // debug("CAD Finished");
    }
}

void LRTP::handleReceiveTimeout(unsigned long t) {
    // fix to prevent getting stuck in RECEIVE state if a corrupt/partial packet
    // is received and onPacketReceive callback is never called
    if (t - m_timer_checkReceiveTimeout >= LORA_SIGNAL_TIMEOUT) {
        bool receiving = LoRa.rxSignalDetected();
        if (receiving) {
            m_checkReceiveRounds = LORA_SIGNAL_TIMEOUT_ROUNDS;
        } else if (m_checkReceiveRounds <= 1) {
            // no signal has been detected, switch back to idle state
            setState(LoRaState::IDLE_RECEIVE);
        } else {
            m_checkReceiveRounds--;
        }
        m_timer_checkReceiveTimeout = t;
    }
}

void LRTP::handleCADDone(unsigned long t) {
    // transmit after CAD finishes

    lrtp_infof("\nCAD finished: Busy: %u.\n", m_channelActive);

    lrtp_info("Sending packet");

    LRTPPacket *p = m_nextConnectionForTransmit->getNextTxPacket();

    if (p != nullptr) {
#if LRTP_DEBUG > 3
        debug_print_packet(*p);
#elif LRTP_DEBUG > 1
        debug_print_packet_header(*p);
#endif

        sendPacket(*p);
    } else {
        lrtp_debugf("%s: ERROR: Transmit packet was null!\n", __PRETTY_FUNCTION__);
        setState(LoRaState::IDLE_RECEIVE);
    }
}

void LRTP::setState(LoRaState newState) {
#if LRTP_DEBUG > 2
    Serial.printf("%s: LORA Radio Change State: %s -> %s\n", __PRETTY_FUNCTION__, LORAStateToStr(m_currentLoRaState), LORAStateToStr(newState));
#endif
    m_currentLoRaState = newState;
}

// ======= DEBUG METHODS =======
void debug_print_packet(const LRTPPacket &packet) {
    // print packet headers
    debug_print_packet_header(packet);
    // print the packet payload in hex:
    Serial.println("\nPayload:");
    for (unsigned int i = 0; i < packet.payloadLength; i++) {
        Serial.print(packet.payload[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    // print the raw packet payload (ASCII)
    for (unsigned int i = 0; i < packet.payloadLength; i++) {
        Serial.print((char)packet.payload[i]);
    }
    Serial.println();
}

void debug_print_packet_header(const LRTPPacket &packet) {
    // print version and type header info
    Serial.printf("Version: %u (0x%02X)\n", packet.version, packet.version);
    Serial.printf("Type: %u (0x%02X)\n", packet.payloadType, packet.payloadType);
    // print the raw flags nibble
    Serial.printf("Flags: (0x%02X) ", LRTP::packFlags(packet.flags));
    // print the flags (as parsed)
    Serial.print(packet.flags.syn ? "S " : "- ");
    Serial.print(packet.flags.fin ? "F " : "- ");
    Serial.print(packet.flags.ack ? "A " : "- ");
    // reserved flag bit
    Serial.printf("X\n");
    // size of the remote acknowledgement window in packets
    Serial.printf("Ack Window: %u (0x%02X)\n", packet.ackWindow, packet.ackWindow);
    // source and destiantion addresses
    Serial.printf("Source: %u (0x%04X)\n", packet.src, packet.src);
    Serial.printf("Destination: %u (0x%04X)\n", packet.dest, packet.dest);
    // sequence and acknowledgement numbers
    Serial.printf("Sequence Num: %u (0x%02X)\n", packet.seqNum, packet.seqNum);
    Serial.printf("Acknowledgment Num: %u (0x%02X)\n", packet.ackNum, packet.ackNum);
    // print the packet payload length
    Serial.printf("Payload: %u bytes.\n", packet.payloadLength);
}

const char *LORAStateToStr(LoRaState state) {
    switch (state) {
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
