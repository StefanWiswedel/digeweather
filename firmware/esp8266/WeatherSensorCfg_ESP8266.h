///////////////////////////////////////////////////////////////////////////////
// WeatherSensorCfg_ESP8266.h - Configuration for NodeMCU ESP8266
// Pin mapping for ESP8266 with CC1101
///////////////////////////////////////////////////////////////////////////////

#ifndef WEATHER_SENSOR_CFG_H
#define WEATHER_SENSOR_CFG_H

// Radio chip selection
#define USE_CC1101

// Bresser sensor type
#define BRESSER_5_IN_1

// Pin configuration for NodeMCU ESP8266 with CC1101
//
// NodeMCU ESP8266 SPI Pins (directly accessible on board):
//   D5 = GPIO14 = SCK  (hardware SPI clock)
//   D6 = GPIO12 = MISO (hardware SPI data in)
//   D7 = GPIO13 = MOSI (hardware SPI data out)
//   D8 = GPIO15 = CS   (directly wired to CC1101 CSN)
//
// Interrupt and GPIO pins:
//   D2 = GPIO4  = GDO0 (interrupt from CC1101)
//   D1 = GPIO5  = GDO2 (optional, general purpose)
//
// PIN MAPPING from ESP32 to NodeMCU:
//   ESP32 GPIO 5  (CS)   -> NodeMCU D8 (GPIO15)
//   ESP32 GPIO 18 (SCK)  -> NodeMCU D5 (GPIO14) - handled by hardware SPI
//   ESP32 GPIO 23 (MOSI) -> NodeMCU D7 (GPIO13) - handled by hardware SPI
//   ESP32 GPIO 19 (MISO) -> NodeMCU D6 (GPIO12) - handled by hardware SPI
//   ESP32 GPIO 4  (GDO0) -> NodeMCU D2 (GPIO4)
//   ESP32 GPIO 2  (GDO2) -> NodeMCU D1 (GPIO5)

#define PIN_RECEIVER_CS   15   // D8 - CSN (Chip Select)
#define PIN_RECEIVER_IRQ  4    // D2 - GDO0 (interrupt)
#define PIN_RECEIVER_GPIO 5    // D1 - GDO2 (optional)
#define PIN_RECEIVER_RST  -1   // Not used for CC1101

// Number of sensors to receive
#define MAX_SENSORS_DEFAULT 1

// Frequency for Europe (868 MHz band)
// Use 915.0 for US
#define WEATHER_SENSOR_FREQUENCY 868.3

// Enable weather sensor features
#define WIND_DATA_FLOATINGPOINT

// Debug output (comment out to disable)
// #define _DEBUG_MODE_

#endif // WEATHER_SENSOR_CFG_H
