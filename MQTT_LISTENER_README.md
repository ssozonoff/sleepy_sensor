# MQTT Listener for Sleepy Sensor

Real-time MQTT listener that automatically decodes and displays Sleepy Sensor telemetry data.

## Installation

```bash
# Install required libraries
pip3 install --user paho-mqtt pycryptodome
```

## Quick Start

### Basic Usage (Public Channel)

```bash
# Listen to default topic on your MQTT broker
python3 mqtt_listener.py mqtt.example.com
```

This will:
- Connect to `mqtt.example.com:1883`
- Subscribe to `sleepy_sensor/#`
- Automatically decrypt with all-zeros PSK (public channel)
- Display decoded sensor data as messages arrive

### Custom Topic

```bash
# Listen to specific topic
python3 mqtt_listener.py mqtt.example.com -t "sensors/outdoor/#"
```

### Private Channel (Custom PSK)

```bash
# Use custom PSK (base64 encoded)
python3 mqtt_listener.py mqtt.example.com -k "YWJjZGVmZ2hpamtsbW5vcA=="
```

### With MQTT Authentication

```bash
# Username and password
python3 mqtt_listener.py mqtt.example.com -u myuser -p mypassword
```

### Verbose Mode

```bash
# Show detailed statistics and debugging info
python3 mqtt_listener.py mqtt.example.com -v
```

### TLS/SSL Encrypted Connection

```bash
# Enable TLS (default port 8883)
python3 mqtt_listener.py mqtt.example.com --tls -P 8883

# With self-signed certificate (disable verification)
python3 mqtt_listener.py mqtt.example.com --tls --tls-insecure -P 8883

# With custom CA certificate
python3 mqtt_listener.py mqtt.example.com --tls --ca-certs /path/to/ca.crt -P 8883

# With client certificate authentication
python3 mqtt_listener.py mqtt.example.com --tls \
  --certfile /path/to/client.crt \
  --keyfile /path/to/client.key \
  -P 8883
```

## Command-Line Options

```
usage: mqtt_listener.py [-h] [-P PORT] [-t TOPIC] [-k PSK]
                        [-u USERNAME] [-p PASSWORD]
                        [--tls] [--tls-insecure] [--ca-certs CA_CERTS]
                        [--certfile CERTFILE] [--keyfile KEYFILE]
                        [-v]
                        broker

positional arguments:
  broker                MQTT broker hostname or IP address

optional arguments:
  -h, --help            show help message and exit
  -P, --port PORT       MQTT broker port (default: 1883 for plain, 8883 for TLS)
  -t, --topic TOPIC     MQTT topic to subscribe to (default: sleepy_sensor/#)
  -k, --psk PSK         Pre-shared key for decryption (base64 encoded)
  -u, --username USER   MQTT username
  -p, --password PASS   MQTT password
  -v, --verbose         Verbose output with statistics

TLS/SSL options:
  --tls                 Enable TLS/SSL encryption
  --tls-insecure        Disable certificate verification (for self-signed certs)
  --ca-certs CA_CERTS   Path to CA certificates file
  --certfile CERTFILE   Path to client certificate file
  --keyfile KEYFILE     Path to client key file
```

## Example Output

When a message is received:

```
======================================================================
MQTT PACKET DECODER - Sleepy Sensor
======================================================================

📦 PACKET INFO:
  origin         : Weather Station - Node 1
  timestamp      : 2024-12-13T16:37:04.000000
  direction      : rx

📡 SIGNAL INFO:
  SNR            : 10 dB
  RSSI           : -45 dBm

📊 PAYLOAD:
  RTC Timestamp  : 1734103424 (2024-12-13T07:08:48+00:00)
  Flags          : 0x00
  Encrypted      : False
  Decryption     : ✓ SUCCESS
  Sensor Count   : 3

  🔬 SENSOR READINGS:

    [1] Channel 1: Voltage
        Type  : 0x74
        Value : 4.18 V

    [2] Channel 1: Temperature
        Type  : 0x67
        Value : 22.5 °C

    [3] Channel 1: Relative Humidity
        Type  : 0x68
        Value : 65.3 %

======================================================================
```

## Use Cases

### 1. Live Monitoring

Monitor sensor data in real-time:

```bash
python3 mqtt_listener.py broker.local -v
```

### 2. Data Logging

Redirect output to a log file:

```bash
python3 mqtt_listener.py mqtt.example.com > sensor_log.txt 2>&1
```

### 3. Integration with Home Automation

```bash
# Listen and pipe to another script for processing
python3 mqtt_listener.py mqtt.example.com | python3 process_data.py
```

### 4. Multiple Listeners

Run multiple listeners for different topics:

```bash
# Terminal 1: Indoor sensors
python3 mqtt_listener.py mqtt.local -t "sensors/indoor/#"

# Terminal 2: Outdoor sensors
python3 mqtt_listener.py mqtt.local -t "sensors/outdoor/#"
```

## Troubleshooting

### Connection Refused

```
❌ Connection failed with code 1
```

**Solution**: Check that:
1. MQTT broker is running
2. Broker hostname/IP is correct
3. Port is correct (default: 1883)
4. Firewall allows connection

### Authentication Failed

```
❌ Connection failed with code 5
```

**Solution**: Provide username/password:
```bash
python3 mqtt_listener.py broker.local -u username -p password
```

### Decryption Failed

```
⚠️  Decryption failed - check PSK configuration
```

**Solution**:
1. Check if device is using private channel
2. Get PSK from device: `channel` command
3. Provide PSK to listener: `-k <base64_psk>`

### TLS Certificate Verification Failed

```
ssl.SSLCertVerificationError: certificate verify failed
```

**Solutions**:

**For production** (recommended):
```bash
# Use proper CA certificate
python3 mqtt_listener.py broker.com --tls --ca-certs /path/to/ca.crt -P 8883
```

**For testing with self-signed certificates** (not recommended for production):
```bash
# Disable verification (use with caution!)
python3 mqtt_listener.py broker.local --tls --tls-insecure -P 8883
```

### TLS Connection Timeout

**Check**:
1. Firewall allows port 8883
2. Broker has TLS enabled
3. Correct port specified (`-P 8883`)

### Client Certificate Required

If broker requires client certificates:
```bash
python3 mqtt_listener.py broker.com --tls \
  --ca-certs ca.crt \
  --certfile client.crt \
  --keyfile client.key \
  -P 8883
```

### No Messages Received

**Check**:
1. Topic pattern is correct
2. Devices are publishing to that topic
3. Network connectivity

```bash
# Test with mosquitto_sub
mosquitto_sub -h mqtt.example.com -t "sleepy_sensor/#" -v
```

## Advanced Usage

### Custom Processing

Create a custom callback by modifying the script:

```python
def on_message(self, client, userdata, msg):
    """Custom message handler"""
    payload = msg.payload.decode('utf-8')
    decoded = self.decoder.decode_mqtt_json(payload)

    # Extract specific sensor values
    for sensor in decoded.get("payload", {}).get("sensors", []):
        if sensor["name"] == "Temperature":
            temp = sensor["value"]

            # Custom logic
            if temp > 30:
                print(f"⚠️  High temperature alert: {temp}°C")
                # Send notification, etc.
```

### Database Integration

Log to database:

```python
import sqlite3

def log_to_database(decoded_data):
    conn = sqlite3.connect('sensors.db')
    cursor = conn.cursor()

    for sensor in decoded_data.get("payload", {}).get("sensors", []):
        cursor.execute(
            "INSERT INTO readings (timestamp, sensor, value, unit) VALUES (?, ?, ?, ?)",
            (datetime.now(), sensor["name"], sensor["value"], sensor["unit"])
        )

    conn.commit()
    conn.close()
```

## Systemd Service

Run as a background service on Linux:

1. Create service file `/etc/systemd/system/sleepy-sensor-listener.service`:

```ini
[Unit]
Description=Sleepy Sensor MQTT Listener
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/sleepy_sensor
ExecStart=/usr/bin/python3 /home/pi/sleepy_sensor/mqtt_listener.py mqtt.local
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

2. Enable and start:

```bash
sudo systemctl enable sleepy-sensor-listener
sudo systemctl start sleepy-sensor-listener
sudo systemctl status sleepy-sensor-listener
```

3. View logs:

```bash
sudo journalctl -u sleepy-sensor-listener -f
```

## Files

- **[mqtt_listener.py](mqtt_listener.py)** - Main MQTT listener script
- **[mqtt_decoder.py](mqtt_decoder.py)** - Decoder library (required)
- **[INSTALL.md](INSTALL.md)** - Installation guide

## See Also

- [DECODER_README.md](DECODER_README.md) - Decoder documentation
- [DECRYPTION_GUIDE.md](DECRYPTION_GUIDE.md) - Decryption details
- [decoder_examples.py](decoder_examples.py) - More usage examples
