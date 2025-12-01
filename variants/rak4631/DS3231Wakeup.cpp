#include "DS3231Wakeup.h"
#include <Arduino.h>

DS3231Wakeup::DS3231Wakeup(TwoWire* wire, uint8_t int_pin)
    : _wire(wire), _int_pin(int_pin) {
}

void DS3231Wakeup::begin() {
  // Configure DS3231 control register to enable alarm interrupts
  uint8_t control = readRegister(DS3231_CONTROL_REG);

  // A1IE=1 (enable Alarm 1 interrupt), INTCN=1 (interrupt control), disable square wave
  control = 0x05;
  writeRegister(DS3231_CONTROL_REG, control);

  // Clear any existing alarm flags
  uint8_t status = readRegister(DS3231_STATUS_REG);
  writeRegister(DS3231_STATUS_REG, status & 0xFC); // Clear A1F and A2F bits
}

bool DS3231Wakeup::checkWakeup() {
  // Check DS3231 status register for alarm flag
  MESH_DEBUG_PRINTLN("\n=== Checking RTC Wakeup ===");

  _wire->beginTransmission(DS3231_I2C_ADDRESS);
  _wire->write(DS3231_STATUS_REG);
  uint8_t i2c_result = _wire->endTransmission();

  if (i2c_result != 0) {
    MESH_DEBUG_PRINTLN("I2C read from RTC failed: %d", i2c_result);
    return false;
  }

  _wire->requestFrom(DS3231_I2C_ADDRESS, 1);

  // Wait for data with timeout
  uint32_t timeout_start = millis();
  while (!_wire->available() && (millis() - timeout_start < 100)) {
    delay(1);
  }

  if (_wire->available()) {
    uint8_t status = _wire->read();
    MESH_DEBUG_PRINTLN("RTC Status register: 0x%02X", status);
    MESH_DEBUG_PRINTLN("  A1F (Alarm 1 Flag): %d", (status & 0x01) ? 1 : 0);
    MESH_DEBUG_PRINTLN("  A2F (Alarm 2 Flag): %d", (status & 0x02) ? 1 : 0);

    // Clear the alarm flag
    if (status & 0x01) {  // A1F (Alarm 1 Flag) bit
      MESH_DEBUG_PRINTLN("Alarm 1 triggered! Clearing flag...");
      _wire->beginTransmission(DS3231_I2C_ADDRESS);
      _wire->write(DS3231_STATUS_REG);
      _wire->write(status & ~0x01); // Clear alarm 1 flag
      _wire->endTransmission();
      return true;
    } else {
      MESH_DEBUG_PRINTLN("Alarm 1 not triggered");
    }
  } else {
    MESH_DEBUG_PRINTLN("No data available from RTC");
  }
  return false;
}

bool DS3231Wakeup::setAlarm(uint16_t seconds) {
  MESH_DEBUG_PRINTLN("\n=== DS3231 Alarm Setup ===");

  // Convert seconds to minutes (round up)
  uint32_t minutes = (seconds + 59) / 60;
  MESH_DEBUG_PRINTLN("Sleep duration: %d seconds (%d minutes)", seconds, minutes);

  // Read current time from DS3231
  _wire->beginTransmission(DS3231_I2C_ADDRESS);
  _wire->write(0x00); // Start at seconds register
  if (_wire->endTransmission() != 0) {
    MESH_DEBUG_PRINTLN("ERROR: I2C write to RTC failed");
    return false;
  }

  _wire->requestFrom(DS3231_I2C_ADDRESS, 4);

  if (_wire->available() < 4) {
    MESH_DEBUG_PRINTLN("ERROR: Only %d bytes available from RTC (expected 4)", _wire->available());
    return false;
  }

  // Helper to convert BCD to decimal
  auto bcdToDec = [](uint8_t bcd) -> uint8_t {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
  };

  uint8_t current_sec = bcdToDec(_wire->read() & 0x7F);
  uint8_t current_min = bcdToDec(_wire->read() & 0x7F);
  uint8_t current_hour = bcdToDec(_wire->read() & 0x3F);
  uint8_t current_day = bcdToDec(_wire->read() & 0x07);

  MESH_DEBUG_PRINTLN("Current time: Day %d, %02d:%02d:%02d",
                    current_day, current_hour, current_min, current_sec);

  // Calculate wake-up time
  uint32_t wake_min = current_min + minutes;
  uint32_t wake_hour = current_hour;
  uint32_t wake_day = current_day;

  // Handle overflow
  if (wake_min >= 60) {
    wake_hour += wake_min / 60;
    wake_min = wake_min % 60;
  }

  if (wake_hour >= 24) {
    wake_day += wake_hour / 24;
    wake_hour = wake_hour % 24;
  }

  if (wake_day > 7) {
    wake_day = ((wake_day - 1) % 7) + 1;
  }

  MESH_DEBUG_PRINTLN("Wake time: Day %d, %02d:%02d:00", wake_day, wake_hour, wake_min);

  // Determine alarm mode based on sleep duration
  // For short sleeps (< 60 min): match minutes and seconds only
  // For longer sleeps: match hours, minutes, and seconds
  bool use_hours = (minutes >= 60);
  MESH_DEBUG_PRINTLN("Alarm mode: %s", use_hours ? "LONG (hrs+mins+secs)" : "SHORT (mins+secs)");

  // Set Alarm 1 registers
  // Seconds = 0 (wake at top of minute)
  _wire->beginTransmission(DS3231_I2C_ADDRESS);
  _wire->write(DS3231_ALARM1_BASE);
  _wire->write(0x00); // A1M1=0 (match seconds), value=0
  _wire->endTransmission();

  // Minutes
  _wire->beginTransmission(DS3231_I2C_ADDRESS);
  _wire->write(DS3231_ALARM1_BASE + 1);
  _wire->write(decToBcd(wake_min)); // A1M2=0 (match minutes)
  _wire->endTransmission();

  // Hours
  _wire->beginTransmission(DS3231_I2C_ADDRESS);
  _wire->write(DS3231_ALARM1_BASE + 2);
  if (use_hours) {
    _wire->write(decToBcd(wake_hour)); // A1M3=0 (match hours)
  } else {
    _wire->write(0x80); // A1M3=1 (ignore hours)
  }
  _wire->endTransmission();

  // Day/Date (always ignore)
  _wire->beginTransmission(DS3231_I2C_ADDRESS);
  _wire->write(DS3231_ALARM1_BASE + 3);
  _wire->write(0x80); // A1M4=1 (ignore day/date)
  _wire->endTransmission();

  // Clear alarm flags in status register
  uint8_t status = readRegister(DS3231_STATUS_REG);
  writeRegister(DS3231_STATUS_REG, status & 0xFC); // Clear A1F and A2F

  // Enable Alarm 1 interrupt
  writeRegister(DS3231_CONTROL_REG, 0x05); // A1IE=1, INTCN=1

  MESH_DEBUG_PRINTLN("=== Alarm Setup Complete ===\n");
  return true;
}

uint8_t DS3231Wakeup::readRegister(uint8_t reg) {
  _wire->beginTransmission(DS3231_I2C_ADDRESS);
  _wire->write(reg);
  _wire->endTransmission();
  _wire->requestFrom(DS3231_I2C_ADDRESS, 1);
  return _wire->available() ? _wire->read() : 0;
}

void DS3231Wakeup::writeRegister(uint8_t reg, uint8_t value) {
  _wire->beginTransmission(DS3231_I2C_ADDRESS);
  _wire->write(reg);
  _wire->write(value);
  _wire->endTransmission();
}

uint8_t DS3231Wakeup::decToBcd(uint8_t val) {
  return ((val / 10) << 4) | (val % 10);
}
