#include "RV3028Wakeup.h"
#include <Arduino.h>

RV3028Wakeup::RV3028Wakeup(TwoWire* wire, uint8_t int_pin)
    : _wire(wire), _int_pin(int_pin) {
}

bool RV3028Wakeup::begin() {
  // Initialize I2C communication
  _rtc.initI2C(*_wire);

  // Probe I2C to verify RTC presence
  _wire->beginTransmission(RV3028_I2C_ADDRESS);
  if (_wire->endTransmission() != 0) {
    MESH_DEBUG_PRINTLN("RV-3028 not detected at 0x52");
    return false;
  }

  MESH_DEBUG_PRINTLN("RV-3028 initialized successfully");
  return true;
}

bool RV3028Wakeup::checkWakeup() {
  MESH_DEBUG_PRINTLN("\n=== Checking RTC Wakeup (RV-3028) ===");

  // Read timer countdown value to see if timer was running
  uint8_t timer_lsb = _rtc.readFromRegister(TIMER_VALUE_0_ADDRESS);
  uint8_t timer_msb = _rtc.readFromRegister(TIMER_VALUE_1_ADDRESS);
  uint16_t timer_remaining = timer_lsb | ((timer_msb & 0x0F) << 8);

  // Read control registers
  uint8_t control1 = _rtc.readFromRegister(CONTROL1_REGISTER_ADDRESS);
  uint8_t control2 = _rtc.readFromRegister(CONTROL2_REGISTER_ADDRESS);

  // Read status register to check for timer event flag
  uint8_t status = _rtc.readFromRegister(STATUS_REGISTER_ADDRESS);

  MESH_DEBUG_PRINTLN("RV-3028 Status register: 0x%02X", status);
  MESH_DEBUG_PRINTLN("  TF (Timer Flag): %d", (status & TIMER_EVENT_FLAG) ? 1 : 0);
  MESH_DEBUG_PRINTLN("  AF (Alarm Flag): %d", (status & ALARM_FLAG) ? 1 : 0);
  MESH_DEBUG_PRINTLN("Timer remaining: %d ticks", timer_remaining);
  MESH_DEBUG_PRINTLN("Control1: 0x%02X (TE=%d)", control1, (control1 & TIMER_ENABLE_FLAG) ? 1 : 0);
  MESH_DEBUG_PRINTLN("Control2: 0x%02X (TIE=%d)", control2, (control2 & TIMER_INTERRUPT_ENABLE_FLAG) ? 1 : 0);

  // Check if timer event flag is set
  bool timer_triggered = (status & TIMER_EVENT_FLAG) != 0;

  if (timer_triggered) {
    MESH_DEBUG_PRINTLN("Timer triggered! Clearing flags...");
    // Clear the timer flag
    _rtc.clearInterruptFlags(true, false, false);
    return true;
  } else {
    MESH_DEBUG_PRINTLN("Timer not triggered");
  }

  return false;
}

bool RV3028Wakeup::setAlarm(uint16_t seconds) {
  MESH_DEBUG_PRINTLN("\n=== RV-3028 Timer Setup ===");
  MESH_DEBUG_PRINTLN("Sleep duration: %d seconds", seconds);

  _rtc.disablePeriodicTimeUpdate();
  _rtc.disableAlarm();
  _rtc.enablePeriodicTimer(seconds, TimerClockFrequency::Hz1, false, true);

  MESH_DEBUG_PRINTLN("=== Timer Setup Complete ===\n");
  return true;
}
