#include "SensorMesh.h"

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
    float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);
    MESH_DEBUG_PRINTLN("Battery: %.2fV", batt_voltage);

    // Add your sensor logic here
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

  // Board init
  board.begin();

  //TODO: This is too specific to the DS3231, this should refactored to an interface
  // Load wakeup counter from GPREGRET2 (persists across sleep, resets on power cycle)
  wakeup_count = NRF_POWER->GPREGRET2;
  MESH_DEBUG_PRINTLN("=== WAKEUP #%d at %lu ms ===", wakeup_count, millis());
  wakeup_count++;

  // Initialize radio and mesh
  if (!radio_init()) {
    while(1) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(100);
      digitalWrite(LED_BUILTIN, LOW);
      delay(100);
    }
  }

  fast_rng.begin(radio_get_rng_seed());

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
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  // Initialize state machine
  current_state = SAMPLING;
  awake_start_time = millis();
  state_start_time = awake_start_time;
  sample_count = 0;
  last_sample_time = 0;

  MESH_DEBUG_PRINTLN("Setup complete, entering main loop");

  sensors.begin();
  the_mesh.begin(fs);

  // Set state pointer for exit command
  the_mesh.setStatePointer(&current_state);

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

      // Always send telemetry data
      the_mesh.broadcastTelemetry();
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