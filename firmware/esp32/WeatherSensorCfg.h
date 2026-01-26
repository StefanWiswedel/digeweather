///////////////////////////////////////////////////////////////////////////////
// WeatherSensorCfg.h - Local configuration override
// Place in sketch folder to override library defaults
///////////////////////////////////////////////////////////////////////////////

#ifndef WEATHER_SENSOR_CFG_H
#define WEATHER_SENSOR_CFG_H

// Radio chip selection
#define USE_CC1101

// Bresser sensor type
#define BRESSER_5_IN_1

// Pin configuration for ESP32 with CC1101
// All pins on same side of dev board
#define PIN_RECEIVER_CS   5    // CSN
#define PIN_RECEIVER_IRQ  4    // GDO0 (interrupt)
#define PIN_RECEIVER_GPIO 2    // GDO2 (optional)
#define PIN_RECEIVER_RST  -1   // Not used for CC1101

// Number of sensors to receive
#define MAX_SENSORS_DEFAULT 1

// Frequency for Europe (868 MHz band)
// Use 915.0 for US
#define WEATHER_SENSOR_FREQUENCY 868.3

// Enable weather sensor features
#define WIND_DATA_FLOATINGPOINT
#define WIND_DATA_FLOATINGPOINT

// Debug output (comment out to disable)
// #define _DEBUG_MODE_

#endif // WEATHER_SENSOR_CFG_H
