# Longmynd2 Tuner 2 Implementation Plan

## Executive Summary

This document provides a comprehensive implementation plan for adding dual-tuner support to longmynd2, enabling simultaneous operation of both STV6120 tuners with independent transport streams, status reporting, and MQTT control.

## Hardware Architecture

### Current Single-Tuner Configuration
```
STV6120 Tuner 1 → STV0910 P2 (TOP) Demodulator → Single TS Output
```

### Target Dual-Tuner Configuration
```
STV6120 Tuner 1 → STV0910 P2 (TOP) Demodulator    → TS Output 1
STV6120 Tuner 2 → STV0910 P1 (BOTTOM) Demodulator → TS Output 2
```

### Register Address Mapping
- **P2 (TOP) Demodulator**: 0xf200-0xf2ff (Tuner 1)
- **P1 (BOTTOM) Demodulator**: 0xf400-0xf4ff (Tuner 2)
- **STV6120 Tuner 1**: Registers 0x02-0x08 (CTRL3-STAT1)
- **STV6120 Tuner 2**: Registers 0x0c-0x12 (CTRL12-STAT2)

## Phase 1: Command Line Interface Extensions

### New Command Line Options

```bash
# Tuner 2 Configuration
-f2 <frequency>           # Tuner 2 frequency in kHz (enables tuner 2)
-s2 <symbol_rate>         # Tuner 2 symbol rate in kS/s
-S2 <halfscan_ratio>      # Tuner 2 halfscan width ratio (default: 1.5)
-p2 <h|v>                 # Tuner 2 polarisation (h=horizontal, v=vertical)

# Tuner 2 Transport Stream Output
-T <fifo_path>            # Tuner 2 TS FIFO path (default: longmynd_tuner2_ts)
-I2 <ip_addr> <port>      # Tuner 2 TS UDP output (default: 230.0.0.3:1234)

# Tuner 2 Status Output
-Z <fifo_path>            # Tuner 2 status FIFO path (default: longmynd_tuner2_status)
-S2 <ip_addr> <port>      # Tuner 2 status UDP output

# Tuner 2 MQTT Control
-M2 <broker_ip> <port>    # Tuner 2 MQTT broker (uses tuner2/ topic prefix)
```

### Usage Examples

```bash
# Basic dual-tuner operation
./longmynd2 -f2 741000 -s2 2000 741500 1500

# Dual-tuner with separate UDP outputs
./longmynd2 -f2 741000 -s2 2000 -I2 230.0.0.3 1234 -i 230.0.0.2 1234 741500 1500

# Dual-tuner with FIFO outputs and custom paths
./longmynd2 -f2 741000 -s2 2000 -T /tmp/tuner2_ts -t /tmp/tuner1_ts 741500 1500

# Dual-tuner with independent polarisation control
./longmynd2 -f2 741000 -s2 2000 -p2 v -p h 741500 1500
```

## Phase 2: Data Structure Extensions

### Configuration Structure Extensions

```c
typedef struct {
    // Existing tuner 1 fields...
    
    // Tuner 2 configuration
    bool tuner2_enabled;
    uint32_t tuner2_freq_requested[4];
    uint32_t tuner2_sr_requested[4];
    uint8_t tuner2_freq_index;
    uint8_t tuner2_sr_index;
    float tuner2_halfscan_ratio;
    bool tuner2_port_swap;
    
    // Tuner 2 TS output
    bool tuner2_ts_use_ip;
    char tuner2_ts_fifo_path[128];
    char tuner2_ts_ip_addr[16];
    int tuner2_ts_ip_port;
    
    // Tuner 2 status output
    bool tuner2_status_use_ip;
    bool tuner2_status_use_mqtt;
    char tuner2_status_fifo_path[128];
    char tuner2_status_ip_addr[16];
    int tuner2_status_ip_port;
    
    // Tuner 2 polarisation (independent LNB control)
    bool tuner2_polarisation_supply;
    bool tuner2_polarisation_horizontal;
    
    // Existing fields...
} longmynd_config_t;
```

### Thread Variables Extension

```c
typedef struct {
    uint8_t *main_state_ptr;
    uint8_t *main_err_ptr;
    uint8_t thread_err;
    longmynd_config_t *config;
    longmynd_status_t *status;
    longmynd_status_t *status2;  // Tuner 2 status
    uint8_t tuner_id;            // 1 or 2 for tuner identification
} thread_vars_t;
```

## Phase 3: Hardware Driver Extensions

### STV0910 Demodulator Extensions

```c
// Tuner-aware register address translation
uint16_t stv0910_get_tuner_reg(uint8_t tuner, uint16_t base_reg) {
    if (tuner == 1) return base_reg;        // P2 registers (0xf2xx)
    else return base_reg + 0x200;           // P1 registers (0xf4xx)
}

// New tuner-aware functions
uint8_t stv0910_start_scan_tuner(uint8_t tuner);
uint8_t stv0910_read_scan_state_tuner(uint8_t tuner, uint8_t *state);
uint8_t stv0910_read_constellation_tuner(uint8_t tuner, int8_t *i, int8_t *q);
uint8_t stv0910_read_power_tuner(uint8_t tuner, uint8_t *power_i, uint8_t *power_q);
uint8_t stv0910_read_agc_tuner(uint8_t tuner, uint16_t *agc1, uint16_t *agc2);
uint8_t stv0910_read_errors_tuner(uint8_t tuner, uint32_t *bch, uint32_t *ldpc);
uint8_t stv0910_read_mer_tuner(uint8_t tuner, int32_t *mer);
uint8_t stv0910_read_modcod_tuner(uint8_t tuner, uint32_t *modcod, bool *short_frame, bool *pilots);
```

### STV6120 Tuner Extensions

The STV6120 already supports dual tuner operation. Key functions to extend:

```c
// Enhanced initialization with independent tuner control
uint8_t stv6120_init_dual(uint32_t freq1, uint32_t freq2, bool swap1, bool swap2);

// Individual tuner frequency setting
uint8_t stv6120_set_freq_tuner(uint8_t tuner, uint32_t frequency);

// Tuner status reading
uint8_t stv6120_read_status_tuner(uint8_t tuner, uint8_t *lock_status);
```

## Phase 4: Thread Management

### Dual TS Processing Threads

```c
// Tuner 1 threads (existing)
pthread_t thread_ts1;
pthread_t thread_ts1_parse;

// Tuner 2 threads (new)
pthread_t thread_ts2;
pthread_t thread_ts2_parse;

// Thread functions
void* loop_ts2(void* thread_vars);
void* loop_ts2_parse(void* thread_vars);
```

### Enhanced I2C Thread

```c
void* loop_i2c(void* thread_vars) {
    // Existing tuner 1 state machine...
    
    // Add tuner 2 state machine
    if (config->tuner2_enabled) {
        // Tuner 2 status collection
        stv0910_read_scan_state_tuner(2, &tuner2_state);
        stv0910_read_constellation_tuner(2, &i2, &q2);
        stv0910_read_power_tuner(2, &power_i2, &power_q2);
        
        // Update tuner 2 status structure
        pthread_mutex_lock(&longmynd_status2.mutex);
        longmynd_status2.state = tuner2_state;
        longmynd_status2.power_i = power_i2;
        longmynd_status2.power_q = power_q2;
        pthread_mutex_unlock(&longmynd_status2.mutex);
    }
}
```

## Phase 5: MQTT Extensions

### Dual MQTT Topic Structure

```c
// Tuner 1 topics (existing)
dt/longmynd/rx_state
dt/longmynd/frequency
dt/longmynd/symbolrate
dt/longmynd/mer
dt/longmynd/modcod

// Tuner 2 topics (new)
dt/longmynd/tuner2/rx_state
dt/longmynd/tuner2/frequency
dt/longmynd/tuner2/symbolrate
dt/longmynd/tuner2/mer
dt/longmynd/tuner2/modcod

// Command topics
dt/longmynd/cmd/tuner2/frequency
dt/longmynd/cmd/tuner2/symbolrate
dt/longmynd/cmd/tuner2/scan
```

### MQTT Message Handling Extensions

```c
uint8_t mqtt_status_write_tuner(uint8_t tuner, uint8_t message, uint32_t data, bool *output_ready) {
    char status_topic[255];
    char status_message[255];
    
    if (tuner == 1) {
        sprintf(status_topic, "dt/longmynd/%s", StatusString[message]);
    } else {
        sprintf(status_topic, "dt/longmynd/tuner2/%s", StatusString[message]);
    }
    
    sprintf(status_message, "%i", data);
    mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
    
    return ERROR_NONE;
}
```

## Phase 6: Main Loop Integration

### Initialization Sequence

```c
int main(int argc, char *argv[]) {
    // Parse command line arguments (including tuner 2 options)

    // Initialize hardware (preserve exact order)
    if (err == ERROR_NONE) err = nim_init();

    // Dual demodulator initialization
    if (err == ERROR_NONE) {
        uint32_t sr1 = config.sr_requested[config.sr_index];
        uint32_t sr2 = config.tuner2_enabled ?
                       config.tuner2_sr_requested[config.tuner2_sr_index] : 0;
        err = stv0910_init(sr1, sr2, config.halfscan_ratio, config.tuner2_halfscan_ratio);
    }

    // Dual tuner initialization
    if (err == ERROR_NONE) {
        uint32_t freq1 = config.freq_requested[config.freq_index];
        uint32_t freq2 = config.tuner2_enabled ?
                         config.tuner2_freq_requested[config.tuner2_freq_index] : 0;
        err = stv6120_init(freq1, freq2, config.port_swap);
    }

    // Initialize status structures
    longmynd_status_t longmynd_status2;  // Tuner 2 status
    memset(&longmynd_status2, 0, sizeof(longmynd_status_t));

    // Create threads
    thread_vars_t thread_vars_ts1, thread_vars_ts1_parse;
    thread_vars_t thread_vars_ts2, thread_vars_ts2_parse;
    thread_vars_t thread_vars_i2c, thread_vars_beep;

    // Initialize tuner 1 threads (existing)
    thread_vars_ts1.status = &longmynd_status;
    thread_vars_ts1.tuner_id = 1;
    pthread_create(&thread_vars_ts1.thread, NULL, loop_ts, &thread_vars_ts1);

    // Initialize tuner 2 threads (if enabled)
    if (config.tuner2_enabled) {
        thread_vars_ts2.status = &longmynd_status2;
        thread_vars_ts2.tuner_id = 2;
        pthread_create(&thread_vars_ts2.thread, NULL, loop_ts2, &thread_vars_ts2);
    }

    // Enhanced I2C thread with dual tuner support
    thread_vars_i2c.status = &longmynd_status;
    thread_vars_i2c.status2 = &longmynd_status2;
    pthread_create(&thread_vars_i2c.thread, NULL, loop_i2c, &thread_vars_i2c);
}
```

## Phase 7: Critical Implementation Considerations

### Hardware Timing Requirements

1. **Initialization Order**: STV0910 P2 (TOP) must be initialized before P1 (BOTTOM)
2. **I2C Timing**: Preserve exact register write sequences and delays
3. **PLL Lock Timing**: Maintain STV6120 calibration timeouts
4. **Register Logging**: Extend comprehensive logging to both tuners

### Thread Safety

```c
// Dual status structure protection
pthread_mutex_t status1_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t status2_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread-safe status updates
void update_tuner_status(uint8_t tuner, longmynd_status_t *status_data) {
    if (tuner == 1) {
        pthread_mutex_lock(&longmynd_status.mutex);
        memcpy(&longmynd_status, status_data, sizeof(longmynd_status_t));
        pthread_mutex_unlock(&longmynd_status.mutex);
    } else {
        pthread_mutex_lock(&longmynd_status2.mutex);
        memcpy(&longmynd_status2, status_data, sizeof(longmynd_status_t));
        pthread_mutex_unlock(&longmynd_status2.mutex);
    }
}
```

### Backward Compatibility

- Single tuner operation remains unchanged
- Existing command line options preserved
- Default behavior identical to current implementation
- Tuner 2 only activated when explicitly configured

### Error Handling

```c
// Independent error recovery for each tuner
typedef struct {
    uint8_t tuner1_error_count;
    uint8_t tuner2_error_count;
    uint64_t tuner1_last_error_time;
    uint64_t tuner2_last_error_time;
} dual_tuner_error_state_t;

// Tuner-specific error recovery
void handle_tuner_error(uint8_t tuner, uint8_t error_code) {
    if (tuner == 1) {
        // Tuner 1 error recovery
        if (error_code == ERROR_TUNER_LOCK_TIMEOUT) {
            stv6120_set_freq_tuner(1, config.freq_requested[config.freq_index]);
        }
    } else {
        // Tuner 2 error recovery
        if (error_code == ERROR_TUNER_LOCK_TIMEOUT) {
            stv6120_set_freq_tuner(2, config.tuner2_freq_requested[config.tuner2_freq_index]);
        }
    }
}
```

## Implementation Timeline

### Phase 1 (Week 1): Foundation
- [ ] Command line parsing extensions
- [ ] Data structure modifications
- [ ] Basic configuration validation

### Phase 2 (Week 2): Hardware Drivers
- [ ] STV0910 tuner-aware functions
- [ ] Register address translation
- [ ] Enhanced STV6120 dual tuner support

### Phase 3 (Week 3): Thread Management
- [ ] Dual TS processing threads
- [ ] Enhanced I2C thread
- [ ] Thread synchronization

### Phase 4 (Week 4): Integration & Testing
- [ ] MQTT extensions
- [ ] Main loop integration
- [ ] Comprehensive testing

## Testing Strategy

### Unit Testing
- Individual tuner register operations
- Register address translation validation
- Thread safety verification

### Integration Testing
- Dual tuner initialization sequences
- Independent frequency/SR changes
- Error recovery scenarios

### System Testing
- Simultaneous dual signal reception
- Independent TS output validation
- MQTT command/status verification

## Success Criteria

1. **Functional**: Both tuners operate independently with separate TS outputs
2. **Performance**: No degradation in single tuner operation
3. **Reliability**: Robust error handling and recovery
4. **Compatibility**: Backward compatibility maintained
5. **Monitoring**: Comprehensive status reporting for both tuners
```
