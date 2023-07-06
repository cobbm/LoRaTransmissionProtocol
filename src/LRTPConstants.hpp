#pragma once

#define LORA_SIGNAL_TIMEOUT_ROUNDS 3
#define LORA_SIGNAL_TIMEOUT 250

#define LRTP_BROADCAST_ADDR 0xFFFF

#define LRTP_MAX_PACKET 255
#define LRTP_TX_PACKET_BUFFER_SZ 4
#define LRTP_RX_PACKET_BUFFER_SZ 1
#define LRTP_GLOBAL_RX_BUFFER_SZ 1
#define LRTP_HEADER_SZ 8
#define LRTP_MAX_PAYLOAD_SZ (LRTP_MAX_PACKET - LRTP_HEADER_SZ)

#define LRTP_PACKET_TIMEOUT 7.5 * 1000 // 15 seconds

#define LRTP_PIGGYBACK_TIMEOUT_DIV 6 // 2
#define LRTP_PIGGYBACK_TIMEOUT                                                 \
  (LRTP_PACKET_TIMEOUT / LRTP_PIGGYBACK_TIMEOUT_DIV)

#define LRTP_CAD_ROUNDS 3

#define LRTP_DEFAULT_VERSION 1
#define LRTP_DEFAULT_TYPE 0

#define LRTP_DEFAULT_ACKWIN LRTP_TX_PACKET_BUFFER_SZ

// TODO: Check if this is used?
//
// #define LRTP_DEBUG 4 /* DEBUG */
// #define LRTP_DEBUG 0 /* DEBUG */
#define LRTP_DEBUG 1 /* INFO */

enum class LRTPError {
  NONE,
  INVALID_SYN,
  INVALID_SYN_ACK,
  CLOSE_FIN_ACK,
  INVALID_SYN_ACK_SYN,
  INVALID_STATE,
};

struct LRTPFlags {
  bool syn;
  bool fin;
  bool ack;
};

struct LRTPPacket {
  uint8_t version;
  uint8_t payloadType;
  LRTPFlags flags;
  uint8_t ackWindow;
  uint16_t src;
  uint16_t dest;
  uint8_t seqNum;
  uint8_t ackNum;
  uint8_t *payload;
  size_t payloadLength;
};

enum class LRTPConnState {
  CLOSED,
  CONNECT_SYN,
  CONNECT_SYN_ACK,
  CONNECTED,
  CLOSE_FIN,
  CLOSE_FIN_ACK,
  // CLOSE_END
};
