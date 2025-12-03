#include "SensorMesh.h"

// ============================================================
// CHANNEL DEFINITIONS
// ============================================================

// Standard Telemetry Channels (1-9) - Reserved for system/framework
#define TELEM_CHANNEL_BATTERY       1   // Battery voltage

// Application Telemetry Channels (10+) - CUSTOMIZE THIS SECTION
// Define your sensor channels here
// Example:
// #define APP_CHANNEL_TEMPERATURE     10
// #define APP_CHANNEL_HUMIDITY        11
// #define APP_CHANNEL_PRESSURE        12
// Add your sensors here...

// ============================================================
// APPLICATION SENSOR OBJECTS
// Declare your sensor objects here
// ============================================================
// Example:
// #include <Adafruit_BME280.h>
// Adafruit_BME280 bme;
// bool bme_initialized = false;

// ============================================================

// State machine
enum SensorNodeState {
  SAMPLING,
  PROCESSING,
  ADVERTISING,
  READY_TO_SLEEP,
  INTERACTIVE_MODE  // Stay awake for configuration/debugging
};

class LowPowerSensorMesh : public SensorMesh {
public:
  LowPowerSensorMesh(mesh::MainBoard& board, mesh::Radio& radio,
                     mesh::MillisecondClock& ms, mesh::RNG& rng,
                     mesh::RTCClock& rtc, mesh::MeshTables& tables)
     : SensorMesh(board, radio, ms, rng, rtc, tables) {}

  // Pointer to current state for exit command
  void setStatePointer(SensorNodeState* state_ptr) {
    current_state_ptr = state_ptr;
  }

protected:
  void onSensorDataRead() override {
    // Not used in low-power mode - device sleeps between wake cycles
    // All sensor reading happens in broadcastApplicationTelemetry()
  }

  int querySeriesData(uint32_t start_secs_ago, uint32_t end_secs_ago,
                     MinMaxAvg dest[], int max_num) override {
    return 0; // Not storing series data in low-power mode
  }

  bool handleCustomCommand(uint32_t sender_timestamp, char* command, char* reply) override {
    if (sender_timestamp == 0 && strcmp(command, "exit") == 0) {
      if (current_state_ptr && *current_state_ptr == INTERACTIVE_MODE) {
        *current_state_ptr = READY_TO_SLEEP;
        strcpy(reply, "Exiting interactive mode, going to sleep...");
      } else {
        strcpy(reply, "Not in interactive mode");
      }
      return true;
    }
    return false;
  }

private:
  SensorNodeState* current_state_ptr = nullptr;
};

StdRNG fast_rng;
SimpleMeshTables tables;

static char command[160];
static const size_t MAX_SERIAL_WAIT_MS = 5000;

LowPowerSensorMesh the_mesh(board, radio_driver, *new ArduinoMillis(),
                            fast_rng, rtc_clock, tables);

// ============================================================
// APPLICATION TELEMETRY BROADCAST FUNCTION
// CUSTOMIZE THIS SECTION TO ADD YOUR SENSOR DATA
// ============================================================

void broadcastApplicationTelemetry() {
  // Create telemetry buffer
  CayenneLPP telemetry(MAX_PACKET_PAYLOAD - 5);
  telemetry.reset();

  // === STANDARD TELEMETRY (Always included) ===
  telemetry.addVoltage(TELEM_CHANNEL_BATTERY,
                      board.getBattMilliVolts() / 1000.0f);

  // === APPLICATION TELEMETRY ===
  // CUSTOMIZE THIS SECTION - Add your sensor readings here

  // Example 1: I2C Temperature/Humidity Sensor (BME280, SHT31, etc.)
  // if (bme_initialized) {
  //   float temp = bme.readTemperature();
  //   float humidity = bme.readHumidity();
  //   telemetry.addTemperature(APP_CHANNEL_TEMPERATURE, temp);
  //   telemetry.addRelativeHumidity(APP_CHANNEL_HUMIDITY, humidity);
  //   MESH_DEBUG_PRINTLN("BME280: %.2fC, %.1f%%", temp, humidity);
  // }

  // Example 2: Analog Sensor (soil moisture, light sensor, etc.)
  // int raw_value = analogRead(A0);
  // float analog_value = raw_value * (3.3 / 4095.0);  // For 12-bit ADC
  // telemetry.addAnalogInput(APP_CHANNEL_SENSOR_1, analog_value);
  // MESH_DEBUG_PRINTLN("Analog: %.3fV", analog_value);

  // Example 3: Digital Sensor (door switch, motion detector, etc.)
  // bool digital_state = digitalRead(SENSOR_PIN);
  // telemetry.addDigitalInput(APP_CHANNEL_SENSOR_2, digital_state ? 1 : 0);
  // MESH_DEBUG_PRINTLN("Digital: %s", digital_state ? "HIGH" : "LOW");

  // Example 4: Averaged sensor values from SAMPLING state
  // float avg_sensor = 0;
  // for (int i = 0; i < sample_count; i++) {
  //   avg_sensor += app_sensor_samples[i];
  // }
  // avg_sensor /= sample_count;
  // telemetry.addTemperature(APP_CHANNEL_TEMPERATURE, avg_sensor);
  // MESH_DEBUG_PRINTLN("Avg sensor: %.2f", avg_sensor);

  // === BROADCAST THE TELEMETRY ===
  uint8_t telem_len = telemetry.getSize();
  if (telem_len == 0) {
    MESH_DEBUG_PRINTLN("No telemetry data to broadcast");
    return;
  }

  int offset = 0;

  // Create packet with timestamp + telemetry
  uint8_t temp[5 + MAX_PACKET_PAYLOAD];
  uint32_t timestamp = the_mesh.getRTCClock()->getCurrentTime();
  memcpy(temp, &timestamp, 4);
  offset += 4;

  // Calculate padding length BEFORE setting flags
  uint8_t total_len = offset + 1 + telemetry.getSize();  
  uint8_t padding_len = (16 - (total_len % 16)) % 16;  
  
  uint8_t flags = 0x00;  // Upper 4 bits reserved for future use
  flags |= (padding_len & 0x0F);  // Store padding in lower 4 bits
  temp[offset++] = flags;

  // add CayenneLPP data
  memcpy(&temp[offset], telemetry.getBuffer(), telem_len);
  offset += telemetry.getSize();

  // Create public group datagram
  mesh::GroupChannel public_channel;
  memset(public_channel.hash, 0, sizeof(public_channel.hash));
  memset(public_channel.secret, 0, sizeof(public_channel.secret));

  auto pkt = the_mesh.createGroupDatagram(PAYLOAD_TYPE_GRP_DATA,
                                          public_channel, temp, offset);

  if (pkt) {
    // Use broadcast zone if configured, otherwise standard flood
    const char* zone = the_mesh.getBroadcastZoneName();
    if (zone == NULL) {
      the_mesh.sendFlood(pkt);
      MESH_DEBUG_PRINTLN("Telemetry broadcast (%d bytes) - standard flood", telem_len);
    } else {
      uint16_t codes[2];
      codes[0] = the_mesh.getBroadcastZone().calcTransportCode(pkt);
      codes[1] = 0;
      the_mesh.sendFlood(pkt, codes);
      MESH_DEBUG_PRINTLN("Telemetry broadcast (%d bytes) - zone: %s", telem_len, zone);
    }
  } else {
    MESH_DEBUG_PRINTLN("ERROR: unable to create telemetry packet!");
  }
}

// ============================================================

// Configuration constants (can be moved to preferences later)
static const uint32_t SAMPLE_INTERVAL_MS = 1000;           // 1 second between samples
static const uint8_t NUM_SAMPLES = 10;                      // Number of samples to collect
static const uint32_t MAX_AWAKE_TIME_MS = 5 * 60 * 1000;  // 5 minutes max

static SensorNodeState current_state = SAMPLING;
static uint32_t state_start_time = 0;
static uint32_t awake_start_time = 0;
static uint8_t wakeup_count = 0;
static uint32_t last_interactive_activity = 0;  // Track last command received
static const uint32_t INTERACTIVE_TIMEOUT_MS = 5 * 60 * 1000;  // Exit interactive mode after 60s of inactivity

// Sampling state variables
static float sensor_samples[NUM_SAMPLES];
static int sample_count = 0;
static uint32_t last_sample_time = 0;

// ============================================================
// APPLICATION SENSOR SAMPLING STORAGE (Optional)
// If you want to average multiple samples, declare storage here
// ============================================================
// Example:
// static float app_sensor_samples[NUM_SAMPLES];
// ============================================================

void setup() {
  // Basic initialization
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Serial
  size_t begin_serial_wait_ms = ::millis();
  while (!Serial && (MAX_SERIAL_WAIT_MS > (::millis() - begin_serial_wait_ms))) {
    ;
  }
  Serial.begin(115200);
  delay(1000);
  MESH_DEBUG_PRINTLN("Setup");

  // Board init
  MESH_DEBUG_PRINTLN("Calling board.begin()...");
  board.begin();
  MESH_DEBUG_PRINTLN("board.begin() completed");

  //TODO: This is too specific to the DS3231, this should refactored to an interface
  // Load wakeup counter from GPREGRET2 (persists across sleep, resets on power cycle)
  MESH_DEBUG_PRINTLN("Loading wakeup counter...");
  wakeup_count = NRF_POWER->GPREGRET2;
  MESH_DEBUG_PRINTLN("=== WAKEUP #%d at %lu ms ===", wakeup_count, millis());
  wakeup_count++;

  // Initialize radio and mesh
  MESH_DEBUG_PRINTLN("Initializing radio...");
  if (!radio_init()) {
    while(1) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
    }
  }

  MESH_DEBUG_PRINTLN("Radio initialized successfully");
  fast_rng.begin(radio_get_rng_seed());

  MESH_DEBUG_PRINTLN("Initializing filesystem...");
  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  MESH_DEBUG_PRINTLN("Loading identity...");
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }
  MESH_DEBUG_PRINTLN("Identity loaded");

  // Initialize state machine
  MESH_DEBUG_PRINTLN("Initializing state machine...");
  current_state = SAMPLING;
  awake_start_time = millis();
  state_start_time = awake_start_time;
  sample_count = 0;
  last_sample_time = 0;

  MESH_DEBUG_PRINTLN("Setup complete, entering main loop");

  MESH_DEBUG_PRINTLN("Calling sensors.begin()...");
  sensors.begin();
  MESH_DEBUG_PRINTLN("sensors.begin() completed");

  MESH_DEBUG_PRINTLN("Calling the_mesh.begin()...");
  the_mesh.begin(fs);
  MESH_DEBUG_PRINTLN("the_mesh.begin() completed");

  // ============================================================
  // APPLICATION SENSOR INITIALIZATION
  // CUSTOMIZE THIS SECTION - Initialize your sensors here
  // ============================================================

  // Example 1: I2C Sensor (BME280, SHT31, etc.)
  // Wire.begin();
  // if (bme.begin(0x76)) {
  //   bme_initialized = true;
  //   MESH_DEBUG_PRINTLN("BME280 initialized");
  // } else {
  //   MESH_DEBUG_PRINTLN("BME280 initialization failed!");
  // }

  // Example 2: Analog Sensor
  // pinMode(A0, INPUT);
  // analogReadResolution(12);  // 12-bit ADC on nRF52

  // Example 3: Digital Sensor
  // pinMode(SENSOR_PIN, INPUT_PULLUP);

  // Example 4: UART Sensor
  // Serial1.begin(9600);

  // ============================================================

  // Set state pointer for exit command
  the_mesh.setStatePointer(&current_state);

  MESH_DEBUG_PRINTLN("===== SETUP COMPLETE - ENTERING LOOP() =====");
  // Fall through to loop()
}

void loop() {
  uint32_t now = millis();

  // Safety timeout: force sleep if awake too long (except in interactive mode)
  if (current_state != INTERACTIVE_MODE && now - awake_start_time >= MAX_AWAKE_TIME_MS) {
    MESH_DEBUG_PRINTLN("WARNING: Max awake time (%lu ms) reached, forcing sleep", MAX_AWAKE_TIME_MS);
    current_state = READY_TO_SLEEP;
  }

  switch (current_state) {
    case SAMPLING: {
      // Take samples at configured intervals
      if (now - last_sample_time >= SAMPLE_INTERVAL_MS) {
        sensor_samples[sample_count] = board.getBattMilliVolts() / 1000.0f;

        // === APPLICATION SENSOR SAMPLING (Optional) ===
        // If you want to average multiple sensor readings, sample them here
        // Example:
        // app_sensor_samples[sample_count] = analogRead(A0) * (3.3 / 4095.0);

        MESH_DEBUG_PRINTLN("Sample %d/%d: %.2fV", sample_count + 1, NUM_SAMPLES, sensor_samples[sample_count]);
        sample_count++;
        last_sample_time = now;

        if (sample_count >= NUM_SAMPLES) {
          current_state = PROCESSING;
          state_start_time = now;
          MESH_DEBUG_PRINTLN("Sampling complete, processing...");
        }
      }
      break;
    }

    case PROCESSING: {
      // Average samples
      float avg = 0;
      for (int i = 0; i < sample_count; i++) {
        avg += sensor_samples[i];
      }
      avg /= sample_count;

      MESH_DEBUG_PRINTLN("Average battery: %.2fV", avg);

      // Broadcast application telemetry (includes battery + custom sensors)
      broadcastApplicationTelemetry();
      MESH_DEBUG_PRINTLN("Telemetry broadcast sent");

      // Decide if we should also advertise (periodic, based on wakeup counter)
      uint8_t wakeups_per_advert = the_mesh.getNodePrefs()->wakeups_per_advert;

      if (wakeup_count >= wakeups_per_advert) {
        MESH_DEBUG_PRINTLN("Wakeup #%d - Time for advertisement!", wakeup_count);
        wakeup_count = 0;
        current_state = ADVERTISING;
      } else {
        MESH_DEBUG_PRINTLN("Wakeup #%d/%d - Skipping advert", wakeup_count, wakeups_per_advert);
        current_state = READY_TO_SLEEP;
      }

      state_start_time = now;
      break;
    }

    case ADVERTISING: {
      the_mesh.sendSelfAdvertisement(16000);
      MESH_DEBUG_PRINTLN("Self-advertisement sent");

      // Wait a bit for TX to complete
      delay(300);

      current_state = READY_TO_SLEEP;
      state_start_time = now;
      break;
    }

    case READY_TO_SLEEP: {
      // Save wakeup counter to GPREGRET2 (persists across sleep cycles)
      NRF_POWER->GPREGRET2 = wakeup_count;
      MESH_DEBUG_PRINTLN("Saved wakeup counter: %d", wakeup_count);

      uint32_t awake_duration = now - awake_start_time;
      MESH_DEBUG_PRINTLN("Awake for %lu ms, entering sleep", awake_duration);

      digitalWrite(LED_BUILTIN, LOW);
      delay(100);

      board.enterLowPowerSleep(the_mesh.getSleepInterval());
      // Never returns
      break;
    }

    case INTERACTIVE_MODE: {
      // Stay awake and responsive - don't sleep
      // Check for inactivity timeout
      if (now - last_interactive_activity >= INTERACTIVE_TIMEOUT_MS) {
        MESH_DEBUG_PRINTLN("Interactive mode timeout, resuming normal operation");
        current_state = READY_TO_SLEEP;
        state_start_time = now;
      }
      break;
    }
  }

  // Keep mesh responsive throughout all states
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

  // Handle serial commands
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {
    command[len - 1] = 0;
    
    char reply[160];
    the_mesh.handleCommand(0, command, reply);
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }
    command[0] = 0;

    // Enter interactive mode when a command is received (unless explicitly exiting)
    if (current_state != INTERACTIVE_MODE && current_state != READY_TO_SLEEP) {
      MESH_DEBUG_PRINTLN("Command received, entering interactive mode, the sleep mode will resume after 60s of inactivity");
      current_state = INTERACTIVE_MODE;
      state_start_time = now;
    }
    // Update last activity time (only if not exiting)
    if (current_state == INTERACTIVE_MODE) {
      last_interactive_activity = now;
    }
  }
}