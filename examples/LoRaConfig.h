#pragma once
// LoRa parameters
//  433E6 for Asia
//  866E6 for Europe
//  915E6 for North America
#define LORA_BAND 433E6
// default: CRC disabled
#define LORA_CRC
// spreading factor: default: 7, range: 6-12
#define LORA_SPREADING_FACTOR 12
// coding rate denominator: default: 5 (4/5), range: 5-8
//#define LORA_CODING_RATE 5
#define LORA_CODING_RATE 5
// default sync word: 0x12
#define LORA_SYNC_WORD 0x4F
// default preamble length: 8
#define LORA_PREAMBLE_LENGTH 8
// default bandwidth:125E3
#define LORA_SIGNAL_BANDWIDTH 125E3

// define the pins used by the transceiver module here:
// ESP32 pins:
#if defined(ESP32)
/* ESP32 pinout:
   ESP32       RFM98       ESP32
          _______________
   --    | ANA     GND   | GND
   --    | AGND    DIO5  | --
   --    | DIO3    RESET | D14
   --    | DIO4    NSS   | D5
   3v3   | 3v3     SCK   | D18
   D2    | DIO0    MOSI  | D23
   --    | DIO1    MISO  | D19
   --    | DIO2    GND   | GND
          ---------------
*/
#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 2

#define LORA_MOSI 23
#define LORA_MISO 19
#define LORA_SCK 18

#elif defined(ESP8266)
/* ESP8266 Pinout:
    ESP8266 Pins    RFM98 Pins
    GND             GND
    3.3V            VCC
    D6              MISO
    D7              MOSI
    D5              SCK
    D8              NSS
    D0              RST
    SD3             DIO0
*/
#define LORA_SS 15
#define LORA_RST 16
#define LORA_DIO0 10

#define LORA_MOSI 7
#define LORA_MISO 6
#define LORA_SCK 5

#endif