# Low-Power Sleeping Sensor for RAK4631 + DS3231 RTC

This firmware provides a foundation for building ultra-low-power sensor nodes using the RAK4631 WisBlock Core module with a DS3231 RTC module for timed wake-up.

## Architecture Overview

The firmware uses a **state machine architecture** that keeps the device awake during sensor processing while remaining responsive to commands and mesh operations. This provides a flexible foundation for complex sensor scenarios while maintaining ultra-low power consumption.

### State Machine

```
┌─────────────────────────────────────────────────────────────┐
│                    WAKE UP (RTC Alarm)                       │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
              ┌────────────────┐
              │    SAMPLING    │ ◄─── Take samples at intervals
              │  (10 samples)  │      (configurable timing & count)
              └────────┬───────┘
                       │
                       ▼
              ┌────────────────┐
              │   PROCESSING   │ ◄─── Average samples
              │  (averaging)   │      Broadcast telemetry (always)
              └────────┬───────┘      Check wakeup counter
                       │
          ┌────────────┴────────────┐
          │                         │
  counter >= threshold?      counter < threshold?
          │                         │
          ▼                         ▼
  ┌──────────────┐          ┌──────────────┐
  │ ADVERTISING  │          │ READY_TO_SLEEP│
  │ (send advert)│          │  (save & sleep)│
  └──────┬───────┘          └──────┬────────┘
         │                         │
         └────────────┬────────────┘
                      │
                      ▼
             ┌────────────────┐
             │ READY_TO_SLEEP │
             │  (enter sleep) │
             └────────┬───────┘
                      │
                      ▼
              ┌────────────────┐
              │ INTERACTIVE    │ ◄─── Command received?
              │     MODE       │      Stay awake, no sleep
              └────────────────┘      60s inactivity timeout
```

### Key Features

- **Active loop architecture**: Loop runs continuously during wake cycles
- **Responsive design**: mesh.loop(), sensors.loop(), serial commands all active
- **State machine**: Clean separation of concerns (SAMPLING → PROCESSING → ADVERTISING → SLEEP)
- **Multi-sample averaging**: Configurable sample count and timing
- **Separate lifecycles**: Telemetry sent every wake, advertisements sent periodically
- **Interactive mode**: Automatic entry when commands received, stays awake for configuration
- **Configurable parameters**: Sample interval, sample count, sleep duration all configurable
- **Safety timeouts**: 5-minute max awake time (except in interactive mode)

## Hardware Requirements

- **RAK4631** - WisBlock Core module (nRF52840 + SX1262)
- **DS3231 RTC Module** - Connected via I2C (address 0x68)
- **RAK19007** or **RAK5005-O** - WisBlock Base Board
- Optional: **RAK1906** or other environment sensors

## Hardware Setup

1. Insert RAK4631 into the WisBlock Base Board CPU slot
2. Connect DS3231 RTC module to I2C pins:
   - SDA: Pin 13 (WB_I2C1_SDA)
   - SCL: Pin 14 (WB_I2C1_SCL)
   - INT/SQW: GPIO 17 (WB_IO1) for wake-up interrupt

## Configuration Constants

The following constants can be adjusted in `main.cpp` (lines 33-36):

```cpp
static const uint32_t SAMPLE_INTERVAL_MS = 1000;           // 1 second between samples
static const uint8_t NUM_SAMPLES = 10;                      // Number of samples to collect
static const uint32_t MAX_AWAKE_TIME_MS = 5 * 60 * 1000;  // 5 minutes max awake time
static const uint32_t INTERACTIVE_TIMEOUT_MS = 60 * 1000;  // 60s interactive mode timeout
```

**Future**: These will be moved to node preferences for runtime configuration via serial commands.

## Operation Modes

### Normal Operation (State Machine)

**Wake Cycle:**
1. **RTC alarm triggers** → Wake from sleep
2. **SAMPLING state**: Collect samples at configured intervals
   - Takes `NUM_SAMPLES` samples (default: 10)
   - Interval: `SAMPLE_INTERVAL_MS` (default: 1 second)
   - Loop remains active, mesh/sensors responsive
3. **PROCESSING state**: Process collected samples
   - Average samples
   - **Always broadcast telemetry data** (every wake cycle)
   - Check wakeup counter against threshold
4. **ADVERTISING state** (conditional):
   - Only if `wakeup_count >= wakeups_per_advert`
   - Send self-advertisement
   - Reset counter to 0
5. **READY_TO_SLEEP state**:
   - Save wakeup counter to flash
   - Log awake duration
   - Enter system-off sleep mode (60 seconds)

### Interactive Mode

**Entry**: Automatically enters when serial command received

**Behavior**:
- Node stays awake indefinitely
- All loops remain active (mesh, sensors, serial)
- Can send multiple commands
- Each command resets 60-second inactivity timer
- After 60s of no activity → transitions to READY_TO_SLEEP

**Use cases**:
- Configuration/debugging
- Real-time sensor monitoring
- Firmware updates via serial
- Manual mesh operations

**Exit**: Automatic after 60s inactivity, or manual via sleep command

## Telemetry vs Advertisement Lifecycles

This firmware implements **separate lifecycles** for telemetry and advertisements:

### Telemetry Broadcast (Every Wake Cycle)

- **Frequency**: Every wake cycle (e.g., every 60 seconds)
- **Content**: Sensor readings (battery voltage, temperature, etc.)
- **Format**: CayenneLPP via group messages
- **Purpose**: Continuous data reporting

### Self-Advertisement (Periodic)

- **Frequency**: Based on `wakeups_per_advert` counter
- **Content**: Node identity, timestamp, name
- **Purpose**: Mesh network presence, discovery, routing
- **Example**: If `wakeups_per_advert = 12` and wake every 60s → advertise every 12 minutes

**Rationale**: Sensor data needs to be transmitted frequently, but advertisements (which help establish mesh routing) only need to happen occasionally.

## Building and Uploading

Build with PlatformIO:

```bash
pio run -e RAK_4631_sleeping_sensor
pio run -e RAK_4631_sleeping_sensor -t upload
```

## Serial Commands

### Sleep Configuration

```
sleep set <seconds>   - Set sleep interval (default: 60)
sleep status          - Show current sleep configuration
```

### Advertisement Configuration

```
advert set <count>    - Set wakeups per advertisement (1-255)
advert status         - Show advertisement frequency
```

**Example:**
```bash
sleep set 60          # Wake every 60 seconds
advert set 12         # Advertise every 12 wakeups (12 minutes)
```

### Zone Configuration

```
zone set <name>       - Set transport code zone for filtering
zone clear            - Clear zone (standard flood)
zone status           - Show current zone configuration
```

Zones use transport codes to limit which repeaters forward packets, reducing network congestion.

## Power Consumption

Typical power consumption with 60-second sleep interval and default settings:

| State | Current | Duration | Notes |
|-------|---------|----------|-------|
| Deep Sleep | 0.4µA | ~47 sec | nRF52 system-off + RTC |
| Sampling | 20mA | ~10 sec | Taking 10 samples @ 1/sec |
| Processing/TX | 130mA | ~3 sec | Telemetry broadcast |
| **Average** | **~8µA** | **60 sec** | Per wake cycle |

**Battery Life Estimate** (3000mAh):
- With hourly advertisement: ~150 days (~5 months)
- With 2-hour advertisement: ~180 days (~6 months)

## Code Structure

```
src/
├── main.cpp                  # State machine implementation
├── SensorMesh.h/cpp          # Sensor mesh base class
├── TimeSeriesData.h/cpp      # Time series data handling

variants/rak4631/
├── RAK4631Board.h            # Board definitions
└── RAK4631Board.cpp          # RTC wake-up, sleep implementation
```

## State Machine Implementation

### State Definitions

```cpp
enum SensorNodeState {
  SAMPLING,         // Collecting sensor samples
  PROCESSING,       // Averaging samples, broadcasting telemetry
  ADVERTISING,      // Sending self-advertisement (periodic)
  READY_TO_SLEEP,   // Saving state and entering sleep
  INTERACTIVE_MODE  // Awake for configuration (command-triggered)
};
```

### Safety Features

1. **Max awake timeout**: 5-minute safety timeout forces sleep if state machine hangs
   - Does NOT apply in interactive mode
   - Prevents battery drain from bugs

2. **Inactivity timeout**: Interactive mode exits after 60s of no commands
   - Automatic return to normal operation
   - Prevents accidentally leaving node awake

3. **State tracking**: All timing tracked with `awake_start_time`, `state_start_time`
   - Easy debugging via serial output
   - Accurate power profiling

## Customization

### Add Custom Sensors

Override `onSensorDataRead()` in the `LowPowerSensorMesh` class:

```cpp
void onSensorDataRead() override {
  float batt_voltage = getVoltage(TELEM_CHANNEL_SELF);
  float temperature = getTemperature(TELEM_CHANNEL_SELF);
  float humidity = getRelativeHumidity(TELEM_CHANNEL_SELF);

  MESH_DEBUG_PRINTLN("Batt: %.2fV, Temp: %.1f°C, RH: %.0f%%",
                     batt_voltage, temperature, humidity);
}
```

### Modify Sampling Behavior

Adjust sampling in the `SAMPLING` state case (main.cpp:107-122):

```cpp
case SAMPLING: {
  if (now - last_sample_time >= SAMPLE_INTERVAL_MS) {
    // Custom sampling logic here
    sensor_samples[sample_count] = readYourSensor();
    sample_count++;
    last_sample_time = now;

    if (sample_count >= NUM_SAMPLES) {
      current_state = PROCESSING;
    }
  }
  break;
}
```

### Add New States

Easy to extend the state machine:

```cpp
enum SensorNodeState {
  WARMING_UP,      // New: Wait for sensor warmup
  SAMPLING,
  PROCESSING,
  CALIBRATING,     // New: Sensor calibration
  ADVERTISING,
  READY_TO_SLEEP,
  INTERACTIVE_MODE
};

// In loop():
case WARMING_UP: {
  if (now - state_start_time >= 5000) {  // 5 second warmup
    current_state = SAMPLING;
    MESH_DEBUG_PRINTLN("Sensor ready, starting sampling");
  }
  break;
}
```

## Technical Details

### RTC Configuration (DS3231)

- **I2C Address**: 0x68
- **Alarm**: Alarm 1 used for wake-up
- **Interrupt**: Active-LOW on INT/SQW pin
- **Accuracy**: ±2ppm (better than RV-3028-C7)

### Wake-up Process

1. DS3231 alarm triggers, pulls INT pin LOW
2. nRF52840 GPIO sense detects LOW on WB_IO1
3. System exits system-off mode, CPU resets
4. `board.begin()` checks RTC alarm flag
5. `setup()` initializes state machine to SAMPLING
6. `loop()` begins executing state machine

### Sleep Process

1. Transition to `READY_TO_SLEEP` state
2. Save wakeup counter to flash
3. Call `board.enterLowPowerSleep(60)`
4. Configure DS3231 alarm for 60 seconds
5. Configure GPIO sense on INT pin
6. Enter nRF52 system-off mode
7. CPU halts (~0.4µA) until GPIO sense triggers

## Troubleshooting

### RTC Not Detected

- Check I2C connections (SDA=13, SCL=14)
- Verify DS3231 module address (0x68)
- Enable debug: `-D MESH_DEBUG=1` in platformio.ini
- Check I2C pull-ups (4.7kΩ typically required)

### Not Waking Up

- Verify INT/SQW connected to WB_IO1 (GPIO 17)
- Check DS3231 alarm configuration
- Ensure SQW/INT pin configured for alarm output
- Monitor serial output for RTC status messages

### Stuck in State

- Check serial output for state transitions
- 5-minute safety timeout should force sleep
- Send serial command to enter interactive mode for debugging

### High Power Consumption

- Verify sleep actually happening (LED should be off)
- Check for infinite loops in state machine
- Monitor awake duration in serial output
- Ensure mesh.loop() not blocking

## Future Enhancements

1. **Runtime configuration**: Move constants to node preferences
2. **Multi-zone broadcasting**: Broadcast to multiple zones simultaneously
3. **Adaptive sampling**: Adjust sample rate based on sensor variance
4. **Low battery mode**: Reduce wake frequency when battery low
5. **Sensor warmup state**: Built-in support for sensors requiring warmup time
6. **Data buffering**: Store multiple telemetry readings when mesh unavailable

## Resources

### Hardware Documentation
- [RAK4631 Documentation](https://docs.rakwireless.com/product-categories/wisblock/rak4631/)
- [DS3231 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf)
- [nRF52840 Low Power Documentation](https://infocenter.nordicsemi.com/topic/ps_nrf52840/power.html)

### MeshCore References
- [MeshCore Repository](https://github.com/meshcore-dev/MeshCore)
- [Transport Codes](../../src/helpers/TransportKeyStore.cpp)
- [Group Messages](../../src/helpers/GroupChannel.cpp)

## License

Same as parent MeshCore project.
