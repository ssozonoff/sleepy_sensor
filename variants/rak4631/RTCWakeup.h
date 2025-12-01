#pragma once

#include <stdint.h>

/**
 * Abstract interface for RTC wakeup functionality
 *
 * Provides minimal interface needed for low-power sleeping sensors:
 * - Check if wake was triggered by RTC alarm
 * - Set next alarm for timed wakeup
 */
class RTCWakeup {
public:
  /**
   * Check if the current wakeup was triggered by an RTC alarm
   * Should clear the alarm flag as a side effect
   *
   * @return true if RTC alarm triggered the wakeup, false otherwise
   */
  virtual bool checkWakeup() = 0;

  /**
   * Set an alarm to trigger after the specified number of seconds
   * Used to schedule the next wakeup before entering low-power sleep
   *
   * @param seconds Number of seconds until alarm should trigger
   * @return true if alarm was set successfully, false on error
   */
  virtual bool setAlarm(uint16_t seconds) = 0;

  virtual ~RTCWakeup() {}
};
