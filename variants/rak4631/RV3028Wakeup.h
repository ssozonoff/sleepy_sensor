#pragma once

#include "RTCWakeup.h"
#include <Wire.h>
#include <Melopero_RV3028.h>
#include <MeshCore.h>

// RV-3028 I2C address (already defined in library, but redefined for clarity)
#define RV3028_I2C_ADDRESS 0x52

/**
 * RV-3028 Real-Time Clock wakeup implementation
 *
 * Provides RTC timer-based wakeup functionality for the RV-3028 RTC module.
 * Uses the countdown timer for timed wakeups and the INT pin for GPIO sense triggering.
 *
 * Key differences from DS3231:
 * - Uses countdown timer (relative) instead of alarm (absolute)
 * - Provides second-level precision instead of minute-level
 * - More power efficient
 * - I2C address: 0x52 (vs DS3231's 0x68)
 */
class RV3028Wakeup : public RTCWakeup {
private:
  Melopero_RV3028 _rtc;
  TwoWire* _wire;
  uint8_t _int_pin;  // GPIO pin connected to RV-3028 INT

public:
  /**
   * Constructor
   *
   * @param wire Pointer to TwoWire I2C interface (default: &Wire)
   * @param int_pin GPIO pin connected to RV-3028 INT (for documentation, not actively used)
   */
  RV3028Wakeup(TwoWire* wire = &Wire, uint8_t int_pin = 0xFF);

  /**
   * Initialize the RV-3028 for wakeup operation
   * Configures INT pin for timer interrupt output
   *
   * @return true if initialization successful, false if RTC not detected or init failed
   */
  bool begin();

  /**
   * Check if wakeup was triggered by RTC timer
   * Reads the status register and checks the Timer Event Flag (TF)
   * Clears the flag if it was set
   *
   * @return true if timer triggered wakeup, false otherwise
   */
  bool checkWakeup() override;

  /**
   * Set alarm to wake after specified number of seconds
   * Uses RV-3028 countdown timer with automatic frequency selection:
   * - Short durations (≤4095s): 1Hz timer for second-level precision
   * - Long durations (>4095s): 1/60Hz timer (minute-level) to support longer sleep
   *
   * @param seconds Number of seconds until wakeup (max ~65535 minutes = 45.5 days)
   * @return true if alarm set successfully, false on error
   */
  bool setAlarm(uint16_t seconds) override;
};
