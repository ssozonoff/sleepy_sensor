# Installation Guide

## Quick Start

### 1. Install Decryption Library

```bash
pip3 install --user pycryptodome
```

### 2. Test the Decoder

```bash
# Test with your encrypted sample packet
python3 test_decryption.py
```

This will decrypt using the **all-zeros PSK** (public channel default).

## Expected Output

If successful, you should see:

```
✓ pycryptodome is installed

======================================================================
TEST: Decrypting with PUBLIC CHANNEL (all-zeros PSK)
======================================================================

🔑 Using PSK: all-zeros (16 bytes) - PUBLIC CHANNEL DEFAULT
   PSK hex: 00000000000000000000000000000000
   Auto-decrypt: True

======================================================================
MQTT PACKET DECODER - Sleepy Sensor
======================================================================

📊 PAYLOAD:
  RTC Timestamp  : [valid timestamp]
  Flags          : 0x00
  Encrypted      : False
  Decryption     : ✓ SUCCESS
  Note           : Packet was successfully decrypted using the provided PSK.
  Sensor Count   : [number of sensors]

  🔬 SENSOR READINGS:
    [1] Channel 1: Voltage
        Value : [battery voltage] V
```

## Troubleshooting

### pycryptodome Not Found

```bash
# Make sure you're installing pycryptodome, NOT pycrypto
pip3 uninstall pycrypto  # Remove old library if present
pip3 install --user pycryptodome
```

### Permission Denied

If you get permission errors:

```bash
# Option 1: Install for user only
pip3 install --user pycryptodome

# Option 2: Use virtual environment
python3 -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
pip install pycryptodome
```

### Decryption Still Fails

If decryption fails with the all-zeros PSK:

1. **Check if using private channel**: Your device may be configured with a custom PSK
   ```bash
   # On device CLI
   channel
   ```

2. **Get the PSK**: If private channel is configured, get the base64 PSK:
   ```python
   import base64
   from mqtt_decoder import MQTTPacketDecoder

   psk = base64.b64decode("your_base64_psk_here")
   decoder = MQTTPacketDecoder(psk=psk)
   ```

3. **Verify packet format**: The "raw" field should contain the encrypted payload

## Usage After Installation

### Basic Decoding (Auto-decrypt)

```python
from mqtt_decoder import MQTTPacketDecoder, pretty_print_decoded

decoder = MQTTPacketDecoder()  # Defaults to all-zeros PSK
result = decoder.decode_mqtt_json(your_mqtt_json)
pretty_print_decoded(result)
```

### Custom PSK

```python
import base64
from mqtt_decoder import MQTTPacketDecoder

# Get PSK from device config
psk = base64.b64decode("YWJjZGVmZ2hpamtsbW5vcA==")

decoder = MQTTPacketDecoder(psk=psk)
result = decoder.decode_mqtt_json(mqtt_json)
```

### MQTT Integration

```python
import paho.mqtt.client as mqtt
from mqtt_decoder import MQTTPacketDecoder

decoder = MQTTPacketDecoder()  # Public channel

def on_message(client, userdata, msg):
    mqtt_json = msg.payload.decode('utf-8')
    result = decoder.decode_mqtt_json(mqtt_json)

    if result["payload"].get("decryption_successful"):
        for sensor in result["payload"]["sensors"]:
            print(f"{sensor['name']}: {sensor['value']} {sensor['unit']}")

client = mqtt.Client()
client.on_message = on_message
client.connect("mqtt.example.com", 1883, 60)
client.subscribe("sleepy_sensor/#")
client.loop_forever()
```

## Files

- **[mqtt_decoder.py](mqtt_decoder.py)** - Main decoder with MeshCore-compatible decryption
- **[test_decryption.py](test_decryption.py)** - Test script
- **[DECRYPTION_GUIDE.md](DECRYPTION_GUIDE.md)** - Detailed decryption documentation
- **[DECODER_README.md](DECODER_README.md)** - General decoder documentation

## Next Steps

After installation:
1. Run `python3 test_decryption.py` to verify decryption works
2. Integrate into your MQTT pipeline
3. See [decoder_examples.py](decoder_examples.py) for more usage examples
