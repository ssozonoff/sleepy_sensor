# Feature: Battery-Aware Hibernate Mode

**Status**: Planned
**Version**: 1.0
**Last Updated**: 2025-12-04

## Overview

This feature adds intelligent power management to the sleepy_sensor application by implementing a battery-aware hibernate mode. When battery voltage drops below a critical threshold, the device enters a progressive power-saving mode that reduces transmission frequency and extends sleep intervals to preserve remaining battery capacity.

## Business Value

- **Extended deployment time**: Devices can continue operating at reduced capacity when battery is low
- **Graceful degradation**: Remote notification when device enters low-power mode
- **Remote monitoring**: Receivers know device status via telemetry channel
- **Battery protection**: Prevents deep discharge that could damage batteries

## Requirements

### Functional Requirements

#### FR-1: Hibernate Entry
When battery voltage drops below 3.0V:
- Device enters hibernate mode
- Sends ONE telemetry message with hibernate=true flag on CayenneLPP channel 2
- Stops all subsequent transmissions until battery recovers
- Begins progressive sleep interval doubling

#### FR-2: Progressive Power Saving
While in hibernate mode with battery still below threshold:
- Double sleep interval on each wake cycle
- Cap sleep interval at configurable maximum (default: 6 hours)
- Skip all transmissions
- Continue monitoring battery voltage

#### FR-3: Hibernate Exit
When battery voltage rises above 3.1V:
- Exit hibernate mode
- Send recovery telemetry with hibernate=false on channel 2
- Immediately reset sleep interval to configured base value
- Resume normal transmission behavior

#### FR-4: Hysteresis Protection
- Enter hibernate at: < 3.0V
- Exit hibernate at: > 3.1V
- 0.1V hysteresis prevents rapid state oscillation

### Non-Functional Requirements

#### NFR-1: State Persistence
- Hibernate state must persist across sleep/wake cycles
- Use GPREGRET2 register (volatile, resets on power cycle)
- State does not need to survive power cycles

#### NFR-2: Configuration
- Maximum hibernate sleep interval must be configurable via CLI
- Default: 21600 seconds (6 hours)
- Range: 3600-86400 seconds (1-24 hours)

#### NFR-3: Backward Compatibility
- New feature must not break existing functionality
- Devices without low battery continue operating normally
- CayenneLPP channel assignments maintain compatibility

## Technical Design

### State Machine Architecture

The implementation uses dedicated states for clean separation of concerns:

```
enum SensorNodeState {
  SAMPLING,           // Collect sensor samples
  PROCESSING,         // Process samples and check battery
  ADVERTISING,        // Send node advertisement
  READY_TO_SLEEP,     // Prepare for deep sleep
  INTERACTIVE_MODE,   // CLI interaction mode
  HIBERNATE_ENTER,    // NEW: First wake in hibernate - send notification
  HIBERNATE          // NEW: In hibernate - skip transmission, double sleep
};
```

### State Flow Diagram

```
SAMPLING → PROCESSING → {
  Battery < 3.0V (first time)  → HIBERNATE_ENTER → READY_TO_SLEEP
  Battery < 3.0V (subsequent)  → HIBERNATE → READY_TO_SLEEP
  Battery > 3.1V (recovery)    → send telemetry → ADVERTISING/READY_TO_SLEEP
  Battery normal               → send telemetry → ADVERTISING/READY_TO_SLEEP
}
```

### Data Structures

#### GPREGRET2 Bit Packing (8 bits)
```
Bits 0-5 (6 bits): Wakeup counter (0-63)
Bit 6 (1 bit):     Hibernate mode flag
Bit 7 (1 bit):     Transmitted hibernate notification flag
```

#### Runtime State Variables
```cpp
static bool hibernate_mode = false;              // Current hibernate status
static uint32_t current_sleep_interval = 0;      // Current sleep (may be doubled)
static uint32_t base_sleep_interval = 0;         // Original interval for recovery
static bool transmitted_hibernate_flag = false;  // Have we sent hibernate=true?
```

#### Configuration (NodePrefs)
```cpp
uint32_t max_hibernate_sleep_secs;  // Maximum sleep in hibernate (default 21600)
```

### CayenneLPP Integration

**Channel Assignments:**
- Channel 1: Battery voltage (existing)
- Channel 2: Hibernate status (new) - Digital Input (0/1)

**Data Format:**
```cpp
telemetry.addVoltage(TELEM_CHANNEL_BATTERY, voltage);
telemetry.addDigitalInput(TELEM_CHANNEL_HIBERNATE, hibernate_mode ? 1 : 0);
```

### Voltage Thresholds

```cpp
static const float HIBERNATE_ENTER_VOLTAGE = 3.0f;   // Enter threshold
static const float HIBERNATE_EXIT_VOLTAGE = 3.1f;    // Exit threshold (hysteresis)
static const uint32_t DEFAULT_MAX_HIBERNATE_SLEEP = 21600;  // 6 hours
```

## Implementation Details

### File Modifications

#### 1. src/main.cpp (~150 lines changed)
- Add HIBERNATE_ENTER and HIBERNATE states to enum
- Add GPREGRET2 packing macros
- Add hibernate state variables
- Add voltage threshold constants
- Modify setup() to load hibernate state from GPREGRET2
- Initialize base/current sleep intervals
- Replace PROCESSING state with battery check logic
- Add HIBERNATE_ENTER state handler
- Add HIBERNATE state handler
- Modify READY_TO_SLEEP to save packed state and use conditional sleep
- Add hibernate status to broadcastApplicationTelemetry()

#### 2. .pio/libdeps/.../MeshCore/src/helpers/CommonCLI.h (1 line)
- Add max_hibernate_sleep_secs to NodePrefs structure

#### 3. src/SensorMesh.cpp (~30 lines)
- Initialize default max_hibernate_sleep_secs
- Add CLI command: `hibernate set <seconds>`
- Add CLI command: `hibernate status`

### State Handler Implementations

#### HIBERNATE_ENTER State
```cpp
case HIBERNATE_ENTER: {
  // First wake in hibernate mode - send notification ONCE
  MESH_DEBUG_PRINTLN("Hibernate: Sending enter notification");
  broadcastApplicationTelemetry();  // Includes hibernate=true on channel 2
  transmitted_hibernate_flag = true;
  current_state = READY_TO_SLEEP;
  break;
}
```

#### HIBERNATE State
```cpp
case HIBERNATE: {
  // Already in hibernate - skip transmission, double sleep interval
  MESH_DEBUG_PRINTLN("Hibernate: Skipping transmission");

  uint32_t max_sleep = the_mesh.getNodePrefs()->max_hibernate_sleep_secs;
  if (max_sleep == 0) max_sleep = DEFAULT_MAX_HIBERNATE_SLEEP;

  if (current_sleep_interval < max_sleep) {
    uint32_t new_interval = current_sleep_interval * 2;
    if (new_interval > max_sleep) new_interval = max_sleep;
    MESH_DEBUG_PRINTLN("Doubling sleep: %lu -> %lu sec",
                       current_sleep_interval, new_interval);
    current_sleep_interval = new_interval;
  }

  current_state = READY_TO_SLEEP;
  break;
}
```

### CLI Commands

```
hibernate set <seconds>   - Set maximum hibernate sleep interval (3600-86400)
hibernate status          - Display current hibernate configuration
```

**Examples:**
```
> hibernate set 7200
Max hibernate sleep: 7200 sec (2.0 hrs)

> hibernate status
Max hibernate sleep: 21600 sec (6.0 hrs)
```

## Testing Strategy

### Unit Testing

1. **State Persistence Test**
   - Enter hibernate mode
   - Sleep and wake
   - Verify hibernate_mode flag persists
   - Verify wakeup counter increments correctly

2. **Battery Threshold Test**
   - Set battery to 2.9V → verify HIBERNATE_ENTER
   - Set battery to 3.2V → verify exit and recovery transmission
   - Oscillate between 2.95V and 3.05V → verify hysteresis prevents toggling

3. **Sleep Doubling Test**
   - Start with base sleep 300s
   - Verify progression: 300 → 600 → 1200 → 2400 → 4800 → ...
   - Verify caps at max_hibernate_sleep_secs

4. **Transmission Suppression Test**
   - Enter hibernate → verify ONE transmission
   - Remain in hibernate → verify NO subsequent transmissions
   - Exit hibernate → verify recovery transmission

### Integration Testing

5. **End-to-End Hibernate Cycle**
   - Normal operation at 3.5V
   - Battery drops to 2.9V → enter hibernate
   - Progressive sleep doubling over multiple cycles
   - Battery recovers to 3.2V → exit hibernate
   - Verify normal operation resumes

6. **Configuration Test**
   - Use CLI to set max hibernate sleep to 7200s (2 hours)
   - Verify sleep caps at 7200s during hibernate
   - Verify preference persists across reboots

7. **CayenneLPP Validation**
   - Decode telemetry packets
   - Verify channel 1 contains battery voltage
   - Verify channel 2 contains hibernate status (0 or 1)

### Edge Case Testing

8. **Interactive Mode During Hibernate**
   - Enter hibernate mode
   - Send CLI command to enter interactive mode
   - Verify device stays awake
   - Exit interactive mode → verify returns to hibernate state

9. **Long Duration Hibernate**
   - Simulate 24-hour hibernate at max sleep (6 hours)
   - Verify 4 wake cycles
   - Verify state remains consistent

10. **Power Cycle During Hibernate**
    - Enter hibernate mode
    - Power cycle device
    - Verify hibernate state resets (acceptable behavior)

## Acceptance Criteria

- [ ] Device enters hibernate when battery < 3.0V
- [ ] Device exits hibernate when battery > 3.1V
- [ ] Exactly ONE transmission sent when entering hibernate
- [ ] NO transmissions while in hibernate (after initial notification)
- [ ] Sleep interval doubles on each hibernate wake cycle
- [ ] Sleep interval caps at configured maximum
- [ ] Sleep interval resets immediately on recovery
- [ ] Hibernate state persists across sleep cycles
- [ ] CayenneLPP channel 2 correctly indicates hibernate status
- [ ] CLI commands work correctly
- [ ] Configuration persists across reboots
- [ ] Normal operation unaffected when battery is healthy
- [ ] Hysteresis prevents rapid state toggling

## Implementation Checklist

### Phase 1: State Machine Foundation
- [ ] Add HIBERNATE_ENTER and HIBERNATE to state enum
- [ ] Add GPREGRET2 packing macros
- [ ] Add hibernate state variables
- [ ] Modify setup() to load packed state
- [ ] Modify READY_TO_SLEEP to save packed state

### Phase 2: Configuration Infrastructure
- [ ] Add max_hibernate_sleep_secs to CommonCLI.h NodePrefs
- [ ] Add default initialization in SensorMesh.cpp
- [ ] Implement "hibernate set" CLI command
- [ ] Implement "hibernate status" CLI command
- [ ] Test CLI commands and preference persistence

### Phase 3: CayenneLPP Integration
- [ ] Add TELEM_CHANNEL_HIBERNATE definition
- [ ] Add hibernate status to broadcastApplicationTelemetry()
- [ ] Test CayenneLPP encoding

### Phase 4: Core State Handlers
- [ ] Add voltage threshold constants
- [ ] Implement modified PROCESSING state with battery check
- [ ] Implement HIBERNATE_ENTER state handler
- [ ] Implement HIBERNATE state handler
- [ ] Update READY_TO_SLEEP to use conditional sleep interval
- [ ] Initialize base_sleep_interval in setup()

### Phase 5: Testing & Validation
- [ ] Unit test: State persistence
- [ ] Unit test: Battery thresholds
- [ ] Unit test: Sleep doubling
- [ ] Unit test: Transmission suppression
- [ ] Integration test: End-to-end cycle
- [ ] Integration test: Configuration
- [ ] Integration test: CayenneLPP validation
- [ ] Edge case: Interactive mode
- [ ] Edge case: Long duration
- [ ] Edge case: Power cycle

### Phase 6: Documentation
- [ ] Update README.md with hibernate mode documentation
- [ ] Add CLI command documentation
- [ ] Update example code comments
- [ ] Create user guide section

## Usage Examples

### Example 1: Normal Battery (No Hibernate)
```
Setup complete
=== WAKEUP #1 (hibernate=0) ===
Average battery: 3.75V
Telemetry broadcast sent
Sleeping for 300 seconds
```

### Example 2: Entering Hibernate
```
=== WAKEUP #5 (hibernate=0) ===
Average battery: 2.95V
*** ENTERING HIBERNATE (2.95V < 3.00V) ***
Hibernate: Sending enter notification
Sleeping for 300 seconds

=== WAKEUP #6 (hibernate=1) ===
Average battery: 2.92V
Hibernate: Skipping transmission
Doubling sleep: 300 -> 600 sec
Sleeping for 600 seconds

=== WAKEUP #7 (hibernate=1) ===
Average battery: 2.90V
Hibernate: Skipping transmission
Doubling sleep: 600 -> 1200 sec
Sleeping for 1200 seconds
```

### Example 3: Exiting Hibernate
```
=== WAKEUP #10 (hibernate=1) ===
Average battery: 3.15V
*** EXITING HIBERNATE (3.15V > 3.10V) ***
Recovery telemetry sent
Sleeping for 300 seconds

=== WAKEUP #11 (hibernate=0) ===
Average battery: 3.20V
Telemetry broadcast sent
Sleeping for 300 seconds
```

## Performance Impact

### Power Consumption
- **Normal mode**: ~5 wakeups/hour (300s sleep) = 5 transmissions/hour
- **Hibernate mode (max sleep)**: ~1 wakeup/6 hours = 4 transmissions/day
- **Power savings**: ~97% reduction in transmissions and wake cycles

### Network Impact
- Reduced network congestion during low battery conditions
- Receivers notified of device status via telemetry
- No silent failures - explicit hibernate notification

### Battery Life Extension
Assuming 300s base sleep interval and 3V cutoff:
- **Without hibernate**: Device stops transmitting immediately at 3V
- **With hibernate**: Device continues minimal operation, potentially extending useful life by days/weeks depending on battery chemistry and capacity

## Future Enhancements

1. **Adaptive hibernate thresholds** - Adjust based on battery chemistry
2. **Periodic status beacons** - Send minimal status every N hours even in deep hibernate
3. **Battery voltage trending** - Predict low battery before critical threshold
4. **User-configurable thresholds** - CLI commands to adjust 3.0V/3.1V thresholds
5. **Non-volatile hibernate state** - Survive power cycles (use flash instead of GPREGRET2)
6. **Hibernate history logging** - Track hibernate events for diagnostics

## References

- Original implementation plan: `.claude/plans/mutable-swimming-sketch.md`
- CayenneLPP specification: [CayenneLPP Format](https://developers.mydevices.com/cayenne/docs/lora/#lora-cayenne-low-power-payload)
- nRF52 GPREGRET documentation: Nordic nRF52 Power Management
- Main application file: `src/main.cpp`
- State machine documentation: See `src/main.cpp` lines 29-36
