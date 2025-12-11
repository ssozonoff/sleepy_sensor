#include <Arduino.h>
#include "RAK4631Board.h"
#include "DS3231Wakeup.h"

#include <bluefruit.h>
#include <Wire.h>

// Custom startup reason for RTC-based wakeup (not in core MeshCore)
#define BD_STARTUP_RTC_WAKEUP  2

static BLEDfu bledfu;

static void connect_callback(uint16_t conn_handle) {
  (void)conn_handle;
  MESH_DEBUG_PRINTLN("BLE client connected");
}

static void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  (void)conn_handle;
  (void)reason;

  MESH_DEBUG_PRINTLN("BLE client disconnected");
}

void RAK4631Board::begin() {
  MESH_DEBUG_PRINTLN("\n=== Board Startup Debug ===");

  // Initialize I2C for RTC access
  Wire.begin();

  // Create RTC wakeup implementation (DS3231)
  if (!rtc_wakeup) {
    rtc_wakeup = new DS3231Wakeup(&Wire, PIN_RTC_INT);
    static_cast<DS3231Wakeup*>(rtc_wakeup)->begin();
  }

  // Check if wakeup was triggered by RTC alarm
  if (rtc_wakeup->checkWakeup()) {
    startup_reason = BD_STARTUP_RTC_WAKEUP;
    MESH_DEBUG_PRINTLN("RTC alarm triggered - RTC_WAKEUP");
  } else {
    startup_reason = BD_STARTUP_NORMAL;
    MESH_DEBUG_PRINTLN("No RTC alarm - NORMAL startup");
  }

  MESH_DEBUG_PRINTLN("=== Board Startup Complete ===\n");

  pinMode(PIN_VBAT_READ, INPUT);
#ifdef PIN_USER_BTN
  pinMode(PIN_USER_BTN, INPUT_PULLUP);
#endif

#ifdef PIN_USER_BTN_ANA
  pinMode(PIN_USER_BTN_ANA, INPUT_PULLUP);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
#endif

  Wire.begin();

  pinMode(SX126X_POWER_EN, OUTPUT);
  digitalWrite(SX126X_POWER_EN, HIGH);
  delay(10);   // give sx1262 some time to power up

  // Enable 3V3_S power rail for WisBlock sensor modules
  // LOW = 3V3_S ON (P-channel MOSFET)
  pinMode(PIN_3V3_S_EN, OUTPUT);
  digitalWrite(PIN_3V3_S_EN, LOW);
}

void RAK4631Board::powerDownPeripherals() {
  // Power down LoRa radio
  digitalWrite(SX126X_POWER_EN, LOW);

  // Disable LEDs
  #ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, LOW);
  #endif
  #ifdef LED_CONN
  digitalWrite(LED_CONN, LOW);
  #endif

  // Power down 3V3_S rail (all WisBlock sensor modules)
  // HIGH = 3V3_S OFF (P-channel MOSFET gate pulled high)
  // Note: RAK12002 RTC module is powered from main 3V3 rail, not 3V3_S,
  // so it continues to run and can generate wake-up interrupts
  digitalWrite(PIN_3V3_S_EN, HIGH);

  // Put GPS to sleep if present
  // (handled by sensor manager)
}

void RAK4631Board::enterLowPowerSleep(uint32_t sleep_seconds) {
  MESH_DEBUG_PRINTLN("Entering low-power sleep for %d seconds", sleep_seconds);

  // Setup RTC alarm for wakeup
  if (rtc_wakeup) {
    rtc_wakeup->setAlarm(sleep_seconds);
  } else {
    MESH_DEBUG_PRINTLN("ERROR: RTC wakeup not initialized!");
    return;
  }

  // Configure nRF52 to wake on RTC interrupt (active LOW)
  nrf_gpio_cfg_sense_input(PIN_RTC_INT, NRF_GPIO_PIN_PULLUP,
                           NRF_GPIO_PIN_SENSE_LOW);
  MESH_DEBUG_PRINTLN("nRF52 GPIO sense configured on pin %d (active LOW)", PIN_RTC_INT);

  // Power down peripherals
  powerDownPeripherals();

  MESH_DEBUG_PRINTLN("Entering system-off mode...");
  Serial.flush();  // Ensure all serial data is sent
  delay(100);      // Give time for serial transmission

  // Enter system-off mode directly (no SoftDevice dependency)
  // The GPIO sense configuration will wake the system on RTC alarm
  NRF_POWER->SYSTEMOFF = 1;

  // CPU halts here and wakes on GPIO sense interrupt from RTC
  // Should never reach this point
  while(1);
}

bool RAK4631Board::startOTAUpdate(const char* id, char reply[]) {
  // Config the peripheral connection with maximum bandwidth
  // more SRAM required by SoftDevice
  // Note: All config***() function must be called before begin()
  Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
  Bluefruit.configPrphConn(92, BLE_GAP_EVENT_LENGTH_MIN, 16, 16);

  Bluefruit.begin(1, 0);
  // Set max power. Accepted values are: -40, -30, -20, -16, -12, -8, -4, 0, 4
  Bluefruit.setTxPower(4);
  // Set the BLE device name
  Bluefruit.setName("RAK4631_OTA");

  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // To be consistent OTA DFU should be added first if it exists
  bledfu.begin();

  // Set up and start advertising
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();

  /* Start Advertising
    - Enable auto advertising if disconnected
    - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
    - Timeout for fast mode is 30 seconds
    - Start(timeout) with timeout = 0 will advertise forever (until connected)

    For recommended advertising interval
    https://developer.apple.com/library/content/qa/qa1931/_index.html
  */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);   // number of seconds in fast mode
  Bluefruit.Advertising.start(0);             // 0 = Don't stop advertising after n seconds

  uint8_t mac_addr[6];
  memset(mac_addr, 0, sizeof(mac_addr));
  Bluefruit.getAddr(mac_addr);
  sprintf(reply, "OK - mac: %02X:%02X:%02X:%02X:%02X:%02X", 
      mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]);

  return true;
}
