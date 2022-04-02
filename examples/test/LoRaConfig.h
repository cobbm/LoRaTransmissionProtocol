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