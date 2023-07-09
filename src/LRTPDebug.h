#pragma once

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

#define LRTP_REPORT_MESSAGE(__x__, __y__)                                                                                                                      \
    if (LRTP_LOG_LEVEL >= (__x__)) {                                                                                                                           \
        Serial.printf("[LRTP] [%s] %s: %s\n", LRTP_LOG_LEVEL_STR(LRTP_LOG_LEVEL), __PRETTY_FUNCTION__, (__y__));                                               \
    }

#define LRTP_REPORT_MESSAGEF(...)                                                                                                                              \
    {                                                                                                                                                          \
        if (LRTP_LOG_LEVEL >= (FIRST_ARG(__VA_ARGS__))) {                                                                                                      \
            Serial.printf("[LRTP] [%s] %s: ", LRTP_LOG_LEVEL_STR(FIRST_ARG(__VA_ARGS__)), __PRETTY_FUNCTION__);                                                \
            Serial.printf(REST_ARG(__VA_ARGS__));                                                                                                              \
        }                                                                                                                                                      \
    }

//
#define lrtp_debug(__x__) LRTP_REPORT_MESSAGE(4, (__x__))
#define lrtp_debugf(...) LRTP_REPORT_MESSAGEF(4, __VA_ARGS__)

#define lrtp_info(__x__) LRTP_REPORT_MESSAGE(3, (__x__))
#define lrtp_infof(...) LRTP_REPORT_MESSAGEF(3, __VA_ARGS__)

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
#define REPORT_MESSAGEF(...)                                                                                                                                   \
    {}
#define debug(__x__)                                                                                                                                           \
    {}

define debugf(...) {
}
#endif
