# JSON Output Implementation for Longmynd2

## Overview

This document describes the comprehensive JSON output implementation for longmynd2 that provides structured demodulator cycle telemetry data for external tool consumption and monitoring.

## Features

### JSON Output Formats

The implementation supports three JSON output formats:

1. **Full Format** - Complete JSON with all demodulator fields and nested structure
2. **Compact Format** - Condensed JSON with abbreviated field names for reduced bandwidth
3. **Minimal Format** - Essential fields only for basic monitoring

### Command Line Options

#### `-j` - Enable JSON Output
Enables JSON output with default settings (full format, 1000ms interval).

```bash
./longmynd2 -j 741500 1500
```

#### `-J <interval_ms>` - Set JSON Output Interval
Enables JSON output and sets the output interval in milliseconds.

```bash
# Output JSON every 2 seconds
./longmynd2 -J 2000 741500 1500
```

#### `-F <format>` - Set JSON Format
Enables JSON output and sets the format type. Options: `full`, `compact`, `minimal`.

```bash
# Use compact format
./longmynd2 -F compact 741500 1500

# Use minimal format for basic monitoring
./longmynd2 -F minimal 741500 1500
```

#### `-C` - Include Constellation Data
Enables inclusion of constellation data in JSON output (future enhancement).

```bash
./longmynd2 -j -C 741500 1500
```

### JSON Output Structure

#### Full Format Example

```json
{
  "timestamp": 1748928715121,
  "tuner": 1,
  "signal": {
    "power_i": 145,
    "power_q": 140,
    "agc1_gain": 2048,
    "agc2_gain": 1024,
    "lna_gain": 15
  },
  "lock": {
    "demod_state": 4,
    "state_name": "demod_s2",
    "locked": true
  },
  "errors": {
    "viterbi_rate": 0,
    "ber": 400,
    "mer": 52,
    "bch_uncorrected": false,
    "bch_count": 0,
    "ldpc_count": 0
  },
  "frequency": {
    "requested": 741500,
    "offset": 1800,
    "actual": 741501.8
  },
  "modulation": {
    "symbol_rate": 1500,
    "modcod": 32,
    "short_frame": false,
    "pilots": true,
    "rolloff": 2
  }
}
```

#### Compact Format Example

```json
{"ts":1748928715121,"t":1,"pi":145,"pq":140,"a1":2048,"a2":1024,"lna":15,"ds":4,"lck":true,"vit":0,"ber":400,"mer":52,"freq":741501.8,"sr":1500,"mc":32}
```

#### Minimal Format Example

```json
{"ts":1748928715121,"t":1,"lck":true,"mer":52,"freq":741500,"sr":1500}
```

### Field Definitions

#### Signal Fields
- `power_i` / `pi`: I channel power level
- `power_q` / `pq`: Q channel power level  
- `agc1_gain` / `a1`: AGC1 gain value
- `agc2_gain` / `a2`: AGC2 gain value
- `lna_gain` / `lna`: LNA gain value

#### Lock Status Fields
- `demod_state` / `ds`: Demodulator state (0=hunting, 1=found_header, 2=DVB-S2, 3=DVB-S)
- `state_name`: Human-readable state name
- `locked` / `lck`: Boolean indicating signal lock (true for DVB-S2/DVB-S states 2 or 3)

#### Error Rate Fields
- `viterbi_rate` / `vit`: Viterbi error rate (DVB-S)
- `ber`: Bit error rate (DVB-S2)
- `mer`: Modulation error rate (MER) in dB*10
- `bch_uncorrected`: BCH uncorrected errors flag
- `bch_count`: BCH corrected error count
- `ldpc_count`: LDPC corrected error count

#### Frequency Fields
- `requested` / `freq`: Requested frequency in kHz
- `offset`: Carrier frequency offset in Hz
- `actual`: Actual frequency (requested + offset/1000)

#### Modulation Fields
- `symbol_rate` / `sr`: Symbol rate in kSymbols/s
- `modcod` / `mc`: MODCOD value
- `short_frame`: Short frame flag (DVB-S2)
- `pilots`: Pilots flag (DVB-S2)
- `rolloff`: Roll-off factor

## Usage Examples

### Basic JSON Monitoring

```bash
# Enable JSON output with default settings
./longmynd2 -j 741500 1500

# Output JSON every 500ms in compact format
./longmynd2 -J 500 -F compact 741500 1500
```

### External Tool Integration

```bash
# Pipe JSON output to monitoring script
./longmynd2 -F minimal -J 1000 741500 1500 | python3 monitor.py

# Log JSON data to file
./longmynd2 -j 741500 1500 | tee demod_log.json

# Parse with jq for specific fields
./longmynd2 -F compact 741500 1500 | jq '.mer, .lck, .freq'
```

### Combined with Existing Features

```bash
# JSON output + MQTT + multicast TS
./longmynd2 -j -M 192.168.1.111 1883 -i 230.0.0.2 1234 741500 1500

# JSON output + status FIFO + TS FIFO
./longmynd2 -F compact -s status_fifo -t ts_fifo 741500 1500
```

## Implementation Details

### Performance Considerations

- JSON formatting is performed only when output interval has elapsed
- Memory allocation is optimized based on format type
- Minimal format uses ~512 bytes, compact ~1KB, full ~2KB per output
- No impact on real-time demodulation performance

### Thread Safety

- JSON output is called from the I2C thread after status synchronization
- Uses local status copy to avoid mutex contention
- Independent of existing MQTT/FIFO status output

### Compilation

The JSON output module is automatically included in the build:

```bash
make clean && make
```

To disable JSON output at compile time, edit `json_output.h`:

```c
#define ENABLE_JSON_OUTPUT 0
```

## Integration with External Tools

### Python Example

```python
import json
import subprocess
import sys

# Start longmynd2 with JSON output
proc = subprocess.Popen([
    './longmynd2', '-F', 'compact', '-J', '1000', '741500', '1500'
], stdout=subprocess.PIPE, text=True)

try:
    for line in proc.stdout:
        try:
            data = json.loads(line.strip())
            print(f"MER: {data['mer']/10:.1f}dB, Locked: {data['lck']}, Freq: {data['freq']}kHz")
        except json.JSONDecodeError:
            continue  # Skip non-JSON lines
except KeyboardInterrupt:
    proc.terminate()
```

### Shell Script Example

```bash
#!/bin/bash
# Monitor signal quality
./longmynd2 -F minimal -J 2000 741500 1500 | while read -r line; do
    mer=$(echo "$line" | jq -r '.mer // 0')
    locked=$(echo "$line" | jq -r '.lck // false')
    
    if [ "$locked" = "true" ] && [ "$mer" -gt 100 ]; then
        echo "$(date): Good signal - MER: $((mer/10))dB"
    else
        echo "$(date): Poor signal - MER: $((mer/10))dB, Locked: $locked"
    fi
done
```

## Compatibility

- Compatible with all existing longmynd2 features
- Does not interfere with MQTT, FIFO, or UDP status output
- Works with dual-tuner configurations
- Maintains exact hardware timing requirements
- Cross-platform compatible (Windows/Linux)

## Future Enhancements

- Constellation data inclusion (`-C` flag implementation)
- JSON schema validation
- Configurable field selection
- WebSocket JSON streaming
- Prometheus metrics export format
