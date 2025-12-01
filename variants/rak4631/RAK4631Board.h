#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include "RTCWakeup.h"

// LoRa radio module pins for RAK4631
#define  P_LORA_DIO_1   47
#define  P_LORA_NSS     42
#define  P_LORA_RESET  RADIOLIB_NC   // 38
#define  P_LORA_BUSY    46
#define  P_LORA_SCLK    43
#define  P_LORA_MISO    45
#define  P_LORA_MOSI    44
#define  SX126X_POWER_EN  37

//#define PIN_GPS_SDA       13  //GPS SDA pin (output option)
//#define PIN_GPS_SCL       14  //GPS SCL pin (output option)
//#define PIN_GPS_TX        16  //GPS TX pin
//#define PIN_GPS_RX        15  //GPS RX pin
#define PIN_GPS_1PPS      17  //GPS PPS pin
#define GPS_BAUD_RATE   9600
#define GPS_ADDRESS   0x42  //i2c address for GPS
 
#define SX126X_DIO2_AS_RF_SWITCH  true
#define SX126X_DIO3_TCXO_VOLTAGE   1.8

// DS3231 RTC Module
// Default to Slot A, change based on your physical slot
#define PIN_RTC_INT     WB_IO1  // GPIO 17 for Slot A (SQW/INT pin)
// Alternative slots:
// Slot B: WB_IO2 (34)
// Slot C: WB_IO3 (21) or WB_IO4 (4)
// Slot D: WB_IO5 (9) or WB_IO6 (10)

#define DS3231_I2C_ADDRESS  0x68

// DS3231 Register Addresses
#define DS3231_REG_CONTROL      0x0E
#define DS3231_REG_STATUS       0x0F
#define DS3231_REG_ALARM1_SEC   0x07
#define DS3231_REG_ALARM1_MIN   0x08
#define DS3231_REG_ALARM1_HOUR  0x09
#define DS3231_REG_ALARM1_DAY   0x0A

// 3V3_S power control (WisBlock sensor modules)
// WB_IO2 controls P-channel MOSFET: HIGH = 3V3_S OFF, LOW = 3V3_S ON
// Note: RAK12002 RTC is powered from main 3V3 rail, not 3V3_S
#define PIN_3V3_S_EN    WB_IO2  // GPIO 34

// built-ins
#define  PIN_VBAT_READ    5
#define  ADC_MULTIPLIER   (3 * 1.73 * 1.187 * 1000)

class RAK4631Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;
  RTCWakeup* rtc_wakeup;

public:
  RAK4631Board() : startup_reason(0), rtc_wakeup(nullptr) {}

  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

  #define BATTERY_SAMPLES 8

  uint16_t getBattMilliVolts() override {
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / BATTERY_SAMPLES;

    return (ADC_MULTIPLIER * raw) / 4096;
  }

  const char* getManufacturerName() const override {
    return "RAK 4631";
  }

  void reboot() override {
    NVIC_SystemReset();
  }

  void enterLowPowerSleep(uint32_t sleep_seconds);
  void powerDownPeripherals();

  // Access to RTC wakeup implementation (for testing/configuration)
  RTCWakeup* getRTCWakeup() { return rtc_wakeup; }

  bool startOTAUpdate(const char* id, char reply[]) override;
};
