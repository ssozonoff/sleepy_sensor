#pragma once

#include "RTCWakeup.h"
#include <Wire.h>
#include <MeshCore.h>

// DS3231 I2C address and register definitions
#define DS3231_I2C_ADDRESS 0x68
#define DS3231_STATUS_REG  0x0F
#define DS3231_CONTROL_REG 0x0E
#define DS3231_ALARM1_BASE 0x07

// DS3231 Register Addresses
#define DS3231_REG_CONTROL      0x0E
#define DS3231_REG_STATUS       0x0F
#define DS3231_REG_ALARM1_SEC   0x07
#define DS3231_REG_ALARM1_MIN   0x08
#define DS3231_REG_ALARM1_HOUR  0x09
#define DS3231_REG_ALARM1_DAY   0x0A

/**
 * DS3231 Real-Time Clock wakeup implementation
 *
 * Provides RTC alarm-based wakeup functionality for the DS3231 RTC module.
 * Uses Alarm 1 for timed wakeups and the INT/SQW pin for GPIO sense triggering.
 */
class DS3231Wakeup : public RTCWakeup {
private:
  TwoWire* _wire;
  uint8_t _int_pin;  // GPIO pin connected to DS3231 INT/SQW

  /**
   * Read a register from the DS3231 via I2C
   */
  uint8_t readRegister(uint8_t reg);

  /**
   * Write a register to the DS3231 via I2C
   */
  void writeRegister(uint8_t reg, uint8_t value);

  /**
   * Convert decimal to BCD (Binary Coded Decimal)
   */
  uint8_t decToBcd(uint8_t val);

public:
  /**
   * Constructor
   *
   * @param wire Pointer to TwoWire I2C interface (default: &Wire)
   * @param int_pin GPIO pin connected to DS3231 INT/SQW (optional, for future use)
   */
  DS3231Wakeup(TwoWire* wire = &Wire, uint8_t int_pin = 0xFF);

  /**
   * Initialize the DS3231 for wakeup operation
   * Configures INT/SQW pin for alarm output
   */
  void begin();

  // RTCWakeup interface implementation
  bool checkWakeup() override;
  bool setAlarm(uint16_t seconds) override;
};
