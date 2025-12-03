# Ultra-Low-Power Sleeping Sensor for RAK4631 + DS3231 RTC

This firmware is specifically optimized for **battery-powered sensor nodes** that sleep between measurements to maximize battery life. It uses the RAK4631 WisBlock Core module with a DS3231 RTC for timed wake-up, achieving ultra-low sleep current (~0.4µA) for months of operation on a single battery charge.

## Design Philosophy: Sleep-First Architecture

This firmware has been carefully optimized by **removing features incompatible with sleep/wake cycles** and implementing sleep-aware alternatives. Every design decision prioritizes power efficiency and reliability for nodes that spend >99% of their time asleep.

**Key Optimizations:**
- **Removed alert system** - retries and acknowledgments incompatible with sleeping nodes
- **Removed time-based advertisements** - replaced with wakeup counter-based periodic ads
- **Removed temporary radio parameters** - not suitable for nodes that reset on wake
- **Immediate ACL writes** - no lazy writes that could be lost during sleep
- **Added private channel support** - encrypted telemetry broadcasts for secure sensor data
- **Added zone-based broadcasting** - transport codes for selective routing and reduced congestion

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
              └────────────────┘      5min inactivity timeout
```

### Key Features

- **Active loop architecture**: Loop runs continuously during wake cycles
- **Responsive design**: mesh.loop(), sensors.loop(), serial commands all active
- **State machine**: Clean separation of concerns (SAMPLING → PROCESSING → ADVERTISING → SLEEP)
- **Multi-sample averaging**: Configurable sample count and timing
- **Separate lifecycles**: Telemetry sent every wake, advertisements sent periodically
- **Private channel support**: AES-encrypted telemetry broadcasts for secure sensor data
- **Zone-based broadcasting**: Transport codes for selective routing and reduced network congestion
- **Interactive mode**: Automatic entry when commands received, stays awake for configuration
- **Configurable parameters**: Sample interval, sample count, sleep duration all configurable
- **Safety timeouts**: 5-minute max awake time (except in interactive mode)
- **Immediate persistence**: All configuration changes saved immediately to prevent data loss

### Features Removed for Sleep Optimization

To maximize reliability and power efficiency for sleeping nodes, the following features have been intentionally removed:

**Alert System (with retries and acknowledgments)**
- **Why removed**: Alerts require persistent connections and retry logic that are incompatible with nodes that sleep between wake cycles. Acknowledgments cannot be reliably received when the sender is asleep.
- **Alternative**: Implement application-level notifications at the receiver/gateway side based on received telemetry data.

**Time-Based Advertisements**
- **Why removed**: Time-based scheduling requires continuous operation and is unreliable when nodes wake at irregular intervals.
- **Alternative**: Wakeup counter-based advertisements - advertise every N wakeups (e.g., advertise on every 12th wakeup).

**Temporary Radio Parameters**
- **Why removed**: Temporary radio configurations are lost when the node enters system-off sleep mode and the CPU resets on wake.
- **Alternative**: Use permanent configuration via preferences that persist across sleep cycles.

**ACL Lazy Writes**
- **Why removed**: Delayed writes to flash increase the risk of data loss if the node enters sleep before the write completes.
- **Alternative**: Immediate writes to flash ensure configuration changes are persisted before sleep.

**Neighbor Tracking and Time Series Logging**
- **Why removed**: These features require continuous operation and significant RAM/flash resources that are wasted on nodes that only wake briefly.
- **Alternative**: Simple multi-sample averaging during wake cycles, with raw telemetry sent to gateway for storage and analysis.

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

The following constants can be adjusted in [main.cpp:204-206](src/main.cpp#L204-L206):

```cpp
static const uint32_t SAMPLE_INTERVAL_MS = 1000;            // 1 second between samples
static const uint8_t NUM_SAMPLES = 10;                       // Number of samples to collect
static const uint32_t MAX_AWAKE_TIME_MS = 5 * 60 * 1000;   // 5 minutes max awake time
static const uint32_t INTERACTIVE_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes interactive timeout
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
- Each command resets 5-minute inactivity timer
- After 5 minutes of no activity → transitions to READY_TO_SLEEP

**Use cases**:
- Configuration/debugging
- Real-time sensor monitoring
- Firmware updates via serial
- Manual mesh operations

**Exit**: Automatic after 5 minutes of inactivity, or manual via `exit` command

## Telemetry vs Advertisement Lifecycles

This firmware implements **separate lifecycles** for telemetry and advertisements:

### Telemetry Broadcast (Every Wake Cycle)

- **Frequency**: Every wake cycle (e.g., every 60 seconds)
- **Content**: Sensor readings (battery voltage, temperature, etc.)
- **Format**: CayenneLPP via group messages
- **Encryption**: Can be public or AES-encrypted via private channels
- **Routing**: Can use zones (transport codes) for selective forwarding
- **Purpose**: Continuous data reporting

### Self-Advertisement (Periodic)

- **Frequency**: Based on `wakeups_per_advert` counter
- **Content**: Node identity, timestamp, name
- **Encryption**: Always unencrypted (public)
- **Purpose**: Mesh network presence, discovery, routing
- **Example**: If `wakeups_per_advert = 12` and wake every 60s → advertise every 12 minutes

**Rationale**: Sensor data needs to be transmitted frequently and can be encrypted/routed selectively, but advertisements (which help establish mesh routing) only need to happen occasionally and must remain public for network discovery.

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

### Private Channel Configuration (Encrypted Telemetry)

Private channels enable **AES-encrypted telemetry broadcasts** to secure sensor data in untrusted environments. This is ideal for sensitive measurements or multi-tenant deployments.

```
channel set <psk_base64>   - Enable private channel with base64-encoded PSK (16 or 32 bytes)
channel clear              - Disable private channel (revert to public broadcast)
channel status             - Show current channel status
```

**PSK Requirements:**
- Must be base64-encoded
- Supports 128-bit (16 bytes) or 256-bit (32 bytes) keys
- Same PSK must be configured on all nodes and gateways that need to communicate

**Generating a PSK:**
```bash
# Generate 128-bit (16-byte) random key and encode as base64
openssl rand -base64 16

# Generate 256-bit (32-byte) random key and encode as base64
openssl rand -base64 32
```

**Example Configuration:**
```bash
# Enable private channel with 256-bit encryption
channel set m3JKxnP0dF8kL2mN5qR9sT1vU7wX3yZ6aC4eG8hJ0lM=

# Check status
channel status
# Output: Private channel: enabled (hash: A5B2C3D4...)

# Disable encryption
channel clear
# Output: Private channel disabled (public broadcast)
```

**Security Notes:**
- Private channel PSK is **persisted to flash** and survives reboots
- All nodes sharing the same PSK can decrypt each other's telemetry
- Gateway/receiver nodes must be configured with the same PSK to decrypt data
- PSK is stored in plaintext in flash - physical security is required

### Zone Configuration (Selective Routing)

Zones use **transport codes** to enable selective packet forwarding, reducing network congestion by limiting which repeaters forward your packets.

```
zone set <name>       - Set transport code zone for filtering
zone clear            - Clear zone (standard flood)
zone status           - Show current zone configuration
```

**How Zones Work:**
1. Sensor node sets a zone name (e.g., "building-a", "sensor", "floor-2")
2. Packets are tagged with a transport code derived from the zone name
3. Only repeaters configured with matching zones will forward the packets
4. Reduces unnecessary retransmissions and interference

**Example:**
```bash
# Configure sensor to use "building-a" zone
zone set building-a

# Check status
zone status
# Output: Zone: building-a (transport code: 0x1A2B)

# Revert to standard flooding (all repeaters forward)
zone clear
# Output: Zone cleared (standard flood)
```

**Use Cases:**
- **Multi-building deployments**: Separate "building-a", "building-b" zones
- **Floor-based routing**: "floor-1", "floor-2", "floor-3" zones
- **Device type filtering**: "sensor", "actuator", "gateway" zones
- **Tenant isolation**: "tenant-1", "tenant-2" in multi-tenant setups

**Configuration:**
- Zone name is **persisted to flash** and survives reboots
- Default zone: `sensor` (can be changed in firmware or via commands)
- Repeaters must be configured with matching zones to forward packets

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

2. **Inactivity timeout**: Interactive mode exits after 5 minutes of no commands
   - Automatic return to normal operation
   - Prevents accidentally leaving node awake

3. **State tracking**: All timing tracked with `awake_start_time`, `state_start_time`
   - Easy debugging via serial output
   - Accurate power profiling

## Customization

### Add Custom Sensors

-- todo

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

1. **Runtime configuration**: Move sampling constants (interval, count, timeouts) to node preferences for configuration via serial commands
2. **Adaptive sampling**: Adjust sample rate based on sensor variance (e.g., sample more frequently when temperature changing rapidly)
3. **Low battery mode**: Reduce wake frequency and disable non-critical features when battery below threshold
4. **Sensor warmup state**: Built-in support for sensors requiring warmup time (e.g., gas sensors, CO2 sensors)
5. **Data buffering with retry**: Store multiple telemetry readings in flash when mesh unavailable, transmit on next successful wake
6. **Conditional telemetry**: Only transmit when sensor values change beyond a threshold to reduce airtime
7. **Multi-zone broadcasting**: Broadcast to multiple zones simultaneously for redundant routing
8. **Configurable encryption**: Runtime PSK configuration without hardcoding, with key rotation support

## Security Considerations

### Private Channels
- PSK is stored in **plaintext in flash** - physical device security is critical
- Use 256-bit keys (32 bytes) for maximum security
- Rotate keys periodically, especially if devices are lost or compromised
- Keep PSK secret and use secure channels for distribution to nodes/gateways

### Zone-Based Routing
- Zones provide **routing isolation**, not security/encryption
- Zone names are public and can be observed by network monitoring
- Use descriptive but non-sensitive zone names (avoid "vault", "sensitive-lab", etc.)
- Combine zones with private channels for both routing control and data security

### Physical Security
- Nodes contain cryptographic material (identity keys, PSKs)
- If device is physically compromised, assume all keys are compromised
- Consider tamper-evident enclosures for high-security deployments
- Use secure boot/flash encryption on platforms that support it

### Best Practices
1. **Defense in depth**: Use both private channels AND zones for maximum security
2. **Key management**: Maintain a secure registry of PSKs and which nodes use them
3. **Network segmentation**: Use different PSKs for different security zones
4. **Monitoring**: Log and monitor telemetry at gateways for anomalies
5. **Battery monitoring**: Low battery can indicate theft or tampering

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
