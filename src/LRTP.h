#ifndef __LRTP_H__
#define __LRTP_H__

#include <Arduino.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#define ENABLE_DEBUG

#ifdef ENABLE_DEBUG
// #define LRTP_DEBUG
/*
[header.h]
int all_my_great_stuff();    / * forward declare * /
#ifdef IMPLEMENTATION
int all_my_great_stuff()     / * implementation * /
{
  return 1;
}
#endif
*/
#define FIRST_ARG(N, ...) N
#define REST_ARG(N, ...) __VA_ARGS__

// #define REST_ARG(ignore, ...) __VA_ARGS__

const char *LRTP_LOG_LEVEL_STR(int logLevel);

#define LOG_LEVEL 4

#define LRTP_REPORT_MESSAGE(__x__, __y__)                                      \
  if (LOG_LEVEL < (__x__)) {                                                   \
    Serial.printf("[%s] %s: %s\n", LRTP_LOG_LEVEL_STR(LOG_LEVEL),              \
                  __PRETTY_FUNCTION__, (__y__));                               \
  }

#define LRTP_REPORT_MESSAGEF(...)                                              \
  {                                                                            \
    if (LOG_LEVEL < (FIRST_ARG(__VA_ARGS__))) {                                \
      Serial.printf("[%s] %s: ", LRTP_LOG_LEVEL_STR(FIRST_ARG(__VA_ARGS__)),   \
                    __PRETTY_FUNCTION__);                                      \
      Serial.printf(REST_ARG(__VA_ARGS__));                                    \
    }                                                                          \
  }

//
#define lrtp_debug(__x__) LRTP_REPORT_MESSAGE(4, (__x__))

#define lrtp_debugf(...) LRTP_REPORT_MESSAGEF(4, __VA_ARGS__)

/*
#ifdef LOGGING_IMPLEMENTATION

// 0 - panic
// 1 - error
// 2 - warning
// 3 - info
// 4 - debug

const char *LRTP_LOG_LEVEL_STR(int logLevel) {
  switch (logLevel) {
  case 0:
    return "PANIC";
  case 1:
    return "ERR";
  case 2:
    return "WARN";
  case 3:
    return "INFO";
  case 4:
    return "DEBUG";
  default:
    return "UNKNOWN";
  }
}

#endif
*/

#else
#define REPORT_MESSAGE(__x__), (__y__)) { }
#define REPORT_MESSAGEF(...)                                                   \
  {}
#define debug(__x__)                                                           \
  {}

define debugf(...) {}
#endif

#include <lwip/sockets.h>

#include <LoRa.h>
#include <SPI.h>

#include "LRTPConnection.hpp"
#include "LRTPConstants.hpp"

enum class LoRaState {
  IDLE_RECEIVE,
  RECEIVE,
  CAD_STARTED,
  CAD_FINISHED,
  TRANSMIT
};

class LRTP {
public:
  LRTP(uint16_t m_hostAddr);

  std::shared_ptr<LRTPConnection> connect(uint16_t destAddr);

  int begin();

  void loop();

  /**
   * @brief Set a handler to be called when a new client has connected
   *
   */
  void onConnect(std::function<void(std::shared_ptr<LRTPConnection>)> callback);
  /**
   * @brief Set a handler to be called when a broadcast packet is received
   *
   */
  void onBroadcastPacket(std::function<void(const LRTPPacket &)> callback);

  // private:
  /**
   * @brief Parse a raw packet into the struct outPacket from a buffer of given
   * length
   *
   * @param outPacket pointer to a LRTPPacket struct in which to parse the
   * packet into
   * @param buf pointer to the buffer to try to parse a packet from
   * @param len the length of the raw packet in bytes
   * @return int 1 on success, 0 on failure
   */
  static int parsePacket(LRTPPacket *outPacket, uint8_t *buf, size_t len);

  static int parseHeaderFlags(LRTPFlags *outFlags, uint8_t rawFlags);

  static uint8_t packFlags(const LRTPFlags &flags);

private:
  uint16_t m_hostAddr;

  LoRaState m_currentLoRaState;

  unsigned int m_cadRoundsRemaining = 0;

  bool m_channelActive = false;

  // the number of bytes waiting to be read by a connection currently in the
  // receive buffer
  int m_loraRxBytesWaiting = 0;

  // stores the next connection which has a packet waiting to transmit, so it
  // can be used after channel activity detection completes
  std::shared_ptr<LRTPConnection> m_nextConnectionForTransmit = nullptr;

  // a buffer used to hold bytes read from the radio that have not yet been
  // processed by a connection
  uint8_t m_rxBuffer[LRTP_MAX_PACKET * LRTP_GLOBAL_RX_BUFFER_SZ];

  // map from connection address to connection object. used to dispatch data to
  // the correct connection once it has been received.
  std::unordered_map<uint16_t, std::shared_ptr<LRTPConnection>>
      m_activeConnections;

  unsigned int m_checkReceiveRounds = 0;
  unsigned long m_timer_checkReceiveTimeout = 0;
  void handleReceiveTimeout(unsigned long t);
  void handleCADDone(unsigned long t);

  // event handlers
  std::function<void(std::shared_ptr<LRTPConnection>)> _onConnect = nullptr;
  std::function<void(const LRTPPacket &)> _onBroadcastPacket = nullptr;

  // handles receiveing data from the radio during the update loop
  void loopReceive();
  // handles the transmision of a packet during the loop
  void loopTransmit();

  void setState(LoRaState newState);

  /**
   * @brief starts channel activity detection before transmitting a packet.
   * switches to CAD_STARTED state. If this is called while a packet is being
   * received, switches state to RECEIVE and doesn't start CAD (otherwise the
   * packet being received will be dropped by the LoRa radio.)
   *
   * @return true if CAD was successfully started
   * @return false if we're part way through receiving a packet
   */
  bool beginCAD();

  std::vector<uint8_t> preparePacket(const LRTPPacket &);

  void handleIncomingPacket(const LRTPPacket &packet);

  void handleIncomingConnectionPacket(const LRTPPacket &packet);

  void handleIncomingBroadcastPacket(const LRTPPacket &packet);

  // sends a packet once CAD has finished
  void sendPacket(const LRTPPacket &packet);

  // handlers for LoRa async
  void onLoRaPacketReceived(int packetSize);
  void onLoRaTxDone();
  void onLoRaCADDone(bool channelBusy);
};

/* ========== Debug methods ==========*/
void debug_print_packet(const LRTPPacket &packet);
void debug_print_packet_header(const LRTPPacket &packet);

const char *LORAStateToStr(LoRaState state);

#endif
