# STV0910 Demodulator Register Monitoring - Optimized Format

## Overview

This document defines a compact, optimized logging format for STV0910 demodulator register monitoring that reduces verbosity while preserving essential diagnostic information for frequent status updates.

## Current Verbose Format Analysis

### Register Groups Identified from Log Analysis

Based on `read_good_demod_registers_loop_tuner_1.txt`, the following register groups were identified:

1. **AGC Registers** (Lines 1-4): AGC IQ input and AGC2 values
2. **Power Registers** (Lines 5-6): I/Q power measurements  
3. **Constellation Registers** (Lines 7-38): I/Q symbol values (16 samples)
4. **Carrier Frequency** (Lines 39-41): 3-byte carrier frequency
5. **Symbol Rate** (Lines 42-45): 4-byte symbol rate
6. **Error Rates** (Lines 46-54): Viterbi errors, FBER counts and errors
7. **MATYPE** (Lines 55-56): DVB-S2 MATYPE information
8. **Noise Estimation** (Lines 57-58): Noise RAM position and value
9. **MODCOD** (Line 59): Demodulator MODCOD

## Optimized Compact Format

### Format Specification

```
[timestamp] T{tuner} DEMOD: AGC={agc_combined} PWR={power_combined} CONST={constellation_summary} 
                           FREQ={carrier_freq} SR={symbol_rate} ERR={error_summary} 
                           MATYPE={matype} NOISE={noise} MODCOD={modcod}
```

### Field Definitions

#### AGC Combined (agc_combined)
```c
// Combine AGCIQIN (16-bit) and AGC2I (16-bit) into compact format
uint32_t agc_iq = (agc_iqin1 << 8) | agc_iqin0;
uint32_t agc2 = (agc2i1 << 8) | agc2i0;
sprintf(agc_combined, "%04X:%04X", agc_iq, agc2);
// Example: "7FEB:0093"
```

#### Power Combined (power_combined)
```c
// Combine I and Q power into single value
sprintf(power_combined, "I%02X/Q%02X", power_i, power_q);
// Example: "I91/Q8C"
```

#### Constellation Summary (constellation_summary)
```c
// Statistical summary of 16 I/Q constellation samples
int16_t i_avg = 0, q_avg = 0;
uint16_t i_range = 0, q_range = 0;
uint8_t i_min = 255, i_max = 0, q_min = 255, q_max = 0;

for (int n = 0; n < 16; n++) {
    i_avg += constellation_i[n];
    q_avg += constellation_q[n];
    if (constellation_i[n] < i_min) i_min = constellation_i[n];
    if (constellation_i[n] > i_max) i_max = constellation_i[n];
    if (constellation_q[n] < q_min) q_min = constellation_q[n];
    if (constellation_q[n] > q_max) q_max = constellation_q[n];
}
i_avg /= 16; q_avg /= 16;
i_range = i_max - i_min; q_range = q_max - q_min;

sprintf(constellation_summary, "I%02X±%02X/Q%02X±%02X", 
        (uint8_t)i_avg, i_range/2, (uint8_t)q_avg, q_range/2);
// Example: "ID7±15/Q2A±0F"
```

#### Carrier Frequency (carrier_freq)
```c
// Combine 3-byte carrier frequency into single hex value
uint32_t freq = (cfr2 << 16) | (cfr1 << 8) | cfr0;
sprintf(carrier_freq, "%06X", freq);
// Example: "0018A9"
```

#### Symbol Rate (symbol_rate)
```c
// Combine 4-byte symbol rate into single hex value
uint32_t sr = (sfr3 << 24) | (sfr2 << 16) | (sfr1 << 8) | sfr0;
sprintf(symbol_rate, "%08X", sr);
// Example: "02D80000"
```

#### Error Summary (error_summary)
```c
// Combine error counters into compact format
uint64_t fber_count = ((uint64_t)fbercpt4 << 32) | ((uint64_t)fbercpt3 << 24) | 
                      ((uint64_t)fbercpt2 << 16) | ((uint64_t)fbercpt1 << 8) | fbercpt0;
uint32_t fber_err = (fbererr2 << 16) | (fbererr1 << 8) | fbererr0;

if (fber_count > 0) {
    sprintf(error_summary, "V%02X/F%08X:%06X", verror, 
            (uint32_t)(fber_count & 0xFFFFFFFF), fber_err);
} else {
    sprintf(error_summary, "V%02X/F-", verror);
}
// Example: "V00/F00000400:000400" or "V00/F-"
```

#### MATYPE (matype)
```c
// Combine MATYPE bytes
uint16_t matype_val = (matstr1 << 8) | matstr0;
sprintf(matype, "%04X", matype_val);
// Example: "F200"
```

#### Noise (noise)
```c
// Combine noise RAM position and value
sprintf(noise, "%02X@%02X", nosramval, nosrampos);
// Example: "64@94"
```

#### MODCOD (modcod)
```c
// Direct MODCOD value
sprintf(modcod, "%02X", dmdmodcod);
// Example: "20"
```

## Implementation

### Compact Logging Function

```c
void log_demod_status_compact(uint8_t tuner, demod_registers_t *regs) {
    char agc_combined[16], power_combined[16], constellation_summary[32];
    char carrier_freq[16], symbol_rate[16], error_summary[32];
    char matype[8], noise[8], modcod[8];
    
    // Format AGC
    uint32_t agc_iq = (regs->agc_iqin1 << 8) | regs->agc_iqin0;
    uint32_t agc2 = (regs->agc2i1 << 8) | regs->agc2i0;
    sprintf(agc_combined, "%04X:%04X", agc_iq, agc2);
    
    // Format power
    sprintf(power_combined, "I%02X/Q%02X", regs->power_i, regs->power_q);
    
    // Format constellation summary (statistical analysis of 16 samples)
    format_constellation_summary(regs->constellation_samples, constellation_summary);
    
    // Format carrier frequency
    uint32_t freq = (regs->cfr2 << 16) | (regs->cfr1 << 8) | regs->cfr0;
    sprintf(carrier_freq, "%06X", freq);
    
    // Format symbol rate
    uint32_t sr = (regs->sfr3 << 24) | (regs->sfr2 << 16) | (regs->sfr1 << 8) | regs->sfr0;
    sprintf(symbol_rate, "%08X", sr);
    
    // Format error summary
    format_error_summary(regs, error_summary);
    
    // Format MATYPE
    uint16_t matype_val = (regs->matstr1 << 8) | regs->matstr0;
    sprintf(matype, "%04X", matype_val);
    
    // Format noise
    sprintf(noise, "%02X@%02X", regs->nosramval, regs->nosrampos);
    
    // Format MODCOD
    sprintf(modcod, "%02X", regs->dmdmodcod);
    
    // Output compact log line
    printf("[%llu] T%d DEMOD: AGC=%s PWR=%s CONST=%s FREQ=%s SR=%s ERR=%s MATYPE=%s NOISE=%s MODCOD=%s\n",
           monotonic_ms(), tuner, agc_combined, power_combined, constellation_summary,
           carrier_freq, symbol_rate, error_summary, matype, noise, modcod);
}
```

### Register Collection Structure

```c
typedef struct {
    // AGC registers
    uint8_t agc_iqin0, agc_iqin1;
    uint8_t agc2i0, agc2i1;
    
    // Power registers
    uint8_t power_i, power_q;
    
    // Constellation samples (16 I/Q pairs)
    uint8_t constellation_i[16];
    uint8_t constellation_q[16];
    
    // Carrier frequency (3 bytes)
    uint8_t cfr0, cfr1, cfr2;
    
    // Symbol rate (4 bytes)
    uint8_t sfr0, sfr1, sfr2, sfr3;
    
    // Error registers
    uint8_t verror;
    uint8_t fbercpt0, fbercpt1, fbercpt2, fbercpt3, fbercpt4;
    uint8_t fbererr0, fbererr1, fbererr2;
    
    // MATYPE
    uint8_t matstr0, matstr1;
    
    // Noise estimation
    uint8_t nosrampos, nosramval;
    
    // MODCOD
    uint8_t dmdmodcod;
} demod_registers_t;
```

## Example Output Comparison

### Current Verbose Format (59 lines)
```
[1748876143263] STV0910: Reading RSTV0910_P2_AGCIQIN0 (0xf20f) = 0xeb (235) - P2 AGC IQ input LSB [DEMOD_CTRL]
[1748876143264] STV0910: Reading RSTV0910_P2_AGCIQIN1 (0xf20e) = 0x7f (127) - P2 AGC IQ input MSB [DEMOD_CTRL]
[1748876143267] STV0910: Reading RSTV0910_P2_AGC2I0 (0xf237) = 0x93 (147) - P2 AGC2 I0 [DEMOD_CTRL]
... (56 more lines)
```

### New Compact Format (1 line)
```
[1748876143339] T1 DEMOD: AGC=7FEB:0093 PWR=I91/Q8C CONST=ID7±15/Q2A±0F FREQ=0018A9 SR=02D80000 ERR=V00/F00000400:000400 MATYPE=F200 NOISE=64@94 MODCOD=20
```

## Benefits

1. **Space Efficiency**: 59 lines reduced to 1 line (98.3% reduction)
2. **Readability**: All critical information in single line
3. **Parsing**: Easy to parse programmatically
4. **Monitoring**: Suitable for frequent status updates (every 2-3 seconds)
5. **Debugging**: Retains all essential diagnostic information

## Integration with Existing Logging

### Conditional Logging Levels

```c
typedef enum {
    DEMOD_LOG_NONE = 0,
    DEMOD_LOG_COMPACT = 1,
    DEMOD_LOG_VERBOSE = 2
} demod_log_level_t;

void log_demod_registers(uint8_t tuner, demod_registers_t *regs, demod_log_level_t level) {
    switch (level) {
        case DEMOD_LOG_COMPACT:
            log_demod_status_compact(tuner, regs);
            break;
        case DEMOD_LOG_VERBOSE:
            log_demod_status_verbose(tuner, regs);  // Existing detailed logging
            break;
        case DEMOD_LOG_NONE:
        default:
            break;
    }
}
```

### Command Line Control

```bash
# Enable compact demodulator logging every 3 seconds
./longmynd2 --demod-log=compact --demod-interval=3000 741500 1500

# Enable verbose demodulator logging (existing behavior)
./longmynd2 --demod-log=verbose 741500 1500

# Disable demodulator logging
./longmynd2 --demod-log=none 741500 1500
```

This optimized format provides comprehensive demodulator monitoring with minimal log verbosity, making it suitable for continuous operation monitoring and real-time status display.
