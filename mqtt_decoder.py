#!/usr/bin/env python3
"""
Decoder for Sleepy Sensor MQTT JSON messages
Decodes Cayenne LPP formatted telemetry data from the 'raw' payload field
"""

import json
import struct
from datetime import datetime, timezone
from typing import Dict, List, Tuple, Any, Optional

try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import unpad
    CRYPTO_AVAILABLE = True
except ImportError:
    CRYPTO_AVAILABLE = False


class CayenneLPPDecoder:
    """Cayenne LPP (Low Power Payload) decoder"""

    # LPP Data Type definitions (from SensorMesh.cpp)
    LPP_GPS = 0x88
    LPP_ACCELEROMETER = 0x71
    LPP_GYROMETER = 0x86
    LPP_TEMPERATURE = 0x67
    LPP_RELATIVE_HUMIDITY = 0x68
    LPP_BAROMETRIC_PRESSURE = 0x73
    LPP_ALTITUDE = 0x79
    LPP_VOLTAGE = 0x74
    LPP_CURRENT = 0x75
    LPP_POWER = 0x80
    LPP_ANALOG_INPUT = 0x02
    LPP_ANALOG_OUTPUT = 0x03
    LPP_LUMINOSITY = 0x65
    LPP_ENERGY = 0x83
    LPP_DIRECTION = 0x84
    LPP_COLOUR = 0x87
    LPP_GENERIC_SENSOR = 0x64
    LPP_FREQUENCY = 0x92
    LPP_DISTANCE = 0x82

    # Data type metadata: (size_bytes, multiplier, is_signed, name, unit)
    TYPE_INFO = {
        0x88: (9, 1, False, "GPS", "lat/lon/alt"),
        0x71: (6, 1, False, "Accelerometer", "g"),
        0x86: (6, 1, False, "Gyrometer", "deg/s"),
        0x67: (2, 10, True, "Temperature", "°C"),
        0x68: (1, 2, False, "Relative Humidity", "%"),
        0x73: (2, 10, False, "Barometric Pressure", "hPa"),
        0x79: (2, 1, True, "Altitude", "m"),
        0x74: (2, 100, False, "Voltage", "V"),
        0x75: (2, 1000, False, "Current", "A"),
        0x80: (2, 1, False, "Power", "W"),
        0x02: (2, 100, True, "Analog Input", "V"),
        0x03: (2, 100, True, "Analog Output", "V"),
        0x65: (2, 1, False, "Luminosity", "lux"),
        0x83: (4, 1000, False, "Energy", "kWh"),
        0x84: (2, 1, False, "Direction", "°"),
        0x87: (3, 1, False, "Colour", "RGB"),
        0x64: (4, 1, False, "Generic Sensor", ""),
        0x92: (4, 1, False, "Frequency", "Hz"),
        0x82: (4, 1000, False, "Distance", "m"),
    }

    # Standard telemetry channel definitions
    TELEM_CHANNEL_BATTERY = 1

    @staticmethod
    def decode_value(data: bytes, size: int, multiplier: int, is_signed: bool) -> float:
        """
        Decode a value from bytes using the specified size, multiplier, and sign

        Args:
            data: Raw bytes to decode
            size: Number of bytes to read
            multiplier: Divisor to apply (value / multiplier)
            is_signed: Whether to interpret as signed integer

        Returns:
            Decoded floating point value
        """
        # Read bytes as big-endian integer
        value = int.from_bytes(data[:size], byteorder='big', signed=is_signed)

        # Apply multiplier
        return value / multiplier

    @staticmethod
    def decode_gps(data: bytes) -> Dict[str, float]:
        """Decode GPS data (9 bytes: lat, lon, alt)"""
        # GPS is encoded as 3-byte latitude, 3-byte longitude, 3-byte altitude
        # All in signed 24-bit format
        lat = int.from_bytes(data[0:3], byteorder='big', signed=True) / 10000.0
        lon = int.from_bytes(data[3:6], byteorder='big', signed=True) / 10000.0
        alt = int.from_bytes(data[6:9], byteorder='big', signed=True) / 100.0

        return {
            "latitude": lat,
            "longitude": lon,
            "altitude": alt
        }

    @staticmethod
    def decode_accelerometer(data: bytes) -> Dict[str, float]:
        """Decode accelerometer data (6 bytes: x, y, z in milli-g)"""
        x = struct.unpack('>h', data[0:2])[0] / 1000.0
        y = struct.unpack('>h', data[2:4])[0] / 1000.0
        z = struct.unpack('>h', data[4:6])[0] / 1000.0

        return {"x": x, "y": y, "z": z}

    @staticmethod
    def decode_gyrometer(data: bytes) -> Dict[str, float]:
        """Decode gyrometer data (6 bytes: x, y, z in 0.01 deg/s)"""
        x = struct.unpack('>h', data[0:2])[0] / 100.0
        y = struct.unpack('>h', data[2:4])[0] / 100.0
        z = struct.unpack('>h', data[4:6])[0] / 100.0

        return {"x": x, "y": y, "z": z}

    @staticmethod
    def decode_colour(data: bytes) -> Dict[str, int]:
        """Decode RGB colour (3 bytes)"""
        return {
            "r": data[0],
            "g": data[1],
            "b": data[2]
        }

    def decode_lpp(self, lpp_data: bytes) -> List[Dict[str, Any]]:
        """
        Decode Cayenne LPP formatted data

        Args:
            lpp_data: Raw LPP bytes

        Returns:
            List of decoded sensor readings with channel, type, name, value, and unit
        """
        readings = []
        i = 0

        while i + 2 <= len(lpp_data):
            channel = lpp_data[i]
            data_type = lpp_data[i + 1]
            i += 2

            # Channel 0 marks end of data
            if channel == 0:
                break

            # Get type info
            if data_type not in self.TYPE_INFO:
                # Unknown type, skip
                break

            size, multiplier, is_signed, name, unit = self.TYPE_INFO[data_type]

            # Check if we have enough bytes
            if i + size > len(lpp_data):
                break

            value_bytes = lpp_data[i:i + size]
            i += size

            # Decode based on type
            if data_type == self.LPP_GPS:
                value = self.decode_gps(value_bytes)
            elif data_type == self.LPP_ACCELEROMETER:
                value = self.decode_accelerometer(value_bytes)
            elif data_type == self.LPP_GYROMETER:
                value = self.decode_gyrometer(value_bytes)
            elif data_type == self.LPP_COLOUR:
                value = self.decode_colour(value_bytes)
            else:
                value = self.decode_value(value_bytes, size, multiplier, is_signed)

            readings.append({
                "channel": channel,
                "type": f"0x{data_type:02X}",
                "name": name,
                "value": value,
                "unit": unit
            })

        return readings


class MQTTPacketDecoder:
    """Decoder for Sleepy Sensor MQTT packets"""

    # Default PSK for public channel (all zeros, 16 bytes for AES-128)
    PUBLIC_CHANNEL_PSK = bytes(16)

    def __init__(self, psk: Optional[bytes] = None, auto_decrypt: bool = True):
        """
        Initialize decoder

        Args:
            psk: Pre-shared key for decryption (bytes). If None, uses all-zeros (public channel).
                 Can be 16 bytes (AES-128) or 32 bytes (AES-256).
            auto_decrypt: If True, automatically attempt decryption with provided PSK
        """
        self.lpp_decoder = CayenneLPPDecoder()
        self.psk = psk if psk is not None else self.PUBLIC_CHANNEL_PSK
        self.auto_decrypt = auto_decrypt and CRYPTO_AVAILABLE

    def decode_raw_payload(self, raw_hex: str) -> Dict[str, Any]:
        """
        Decode the raw hex payload from the MQTT message

        Payload structure (ENCRYPTED or UNENCRYPTED):
        - Entire payload is encrypted if private channel is used
        - If unencrypted (public channel):
          - Bytes 0-3: RTC timestamp (32-bit, big-endian)
          - Byte 4: Flags (reserved, currently 0x00)
          - Bytes 5+: Cayenne LPP sensor data

        Args:
            raw_hex: Hex string of the raw payload

        Returns:
            Dictionary with timestamp, flags, and decoded sensor readings
        """
        # Convert hex string to bytes
        raw_bytes = bytes.fromhex(raw_hex)

        if len(raw_bytes) < 5:
            return {
                "error": "Payload too short (minimum 5 bytes required)",
                "raw": raw_hex
            }

        # Try to decode as unencrypted packet
        # Extract timestamp (bytes 0-3, big-endian 32-bit)
        timestamp = struct.unpack('>I', raw_bytes[0:4])[0]

        # Extract flags (byte 4)
        flags = raw_bytes[4]

        # Extract and decode LPP data (bytes 5+)
        lpp_data = raw_bytes[5:]
        sensor_readings = self.lpp_decoder.decode_lpp(lpp_data)

        # Detect if packet is likely encrypted
        is_encrypted = self._detect_encryption(raw_bytes, sensor_readings, timestamp)

        # Try to decrypt if encrypted and auto_decrypt is enabled
        decryption_attempted = False
        decryption_successful = False

        if is_encrypted and self.auto_decrypt:
            decryption_attempted = True
            decrypted = self.decrypt_payload(raw_bytes)

            if decrypted:
                # Successfully decrypted, re-parse the decrypted data
                decryption_successful = True
                timestamp = struct.unpack('>I', decrypted[0:4])[0]
                flags = decrypted[4]
                lpp_data = decrypted[5:]
                sensor_readings = self.lpp_decoder.decode_lpp(lpp_data)
                is_encrypted = False  # Mark as decrypted

        result = {
            "rtc_timestamp": timestamp,
            "rtc_datetime": datetime.fromtimestamp(timestamp, timezone.utc).isoformat() if timestamp > 0 and timestamp < 2**31 else "Not set",
            "flags": f"0x{flags:02X}",
            "sensor_count": len(sensor_readings),
            "sensors": sensor_readings,
            "encrypted": is_encrypted,
            "decryption_attempted": decryption_attempted,
            "decryption_successful": decryption_successful
        }

        if is_encrypted:
            result["warning"] = "This packet appears to be ENCRYPTED (private channel). Decryption requires the PSK (Pre-Shared Key) configured on the device."
            result["decryption_help"] = {
                "algorithm": "AES-128 or AES-256 (depending on PSK length)",
                "psk_format": "Base64-encoded, 16 or 32 bytes",
                "how_to_decrypt": "The entire payload (all bytes) is encrypted using the channel's PSK. You need the same PSK that was configured on the transmitting device to decrypt.",
                "device_config": "Check device settings for 'private_channel_psk' or use CLI command 'channel' to view/set",
                "crypto_available": CRYPTO_AVAILABLE
            }

            if decryption_attempted and not decryption_successful:
                result["decryption_note"] = "Auto-decryption was attempted but failed. The PSK may be incorrect, or the encryption mode may not be supported."

        if decryption_successful:
            result["decryption_note"] = "Packet was successfully decrypted using the provided PSK."

        return result

    def _detect_encryption(self, raw_bytes: bytes, sensor_readings: List, timestamp: int) -> bool:
        """
        Detect if a packet is likely encrypted

        Heuristics:
        - No valid sensor readings decoded
        - First LPP channel is 0 (end marker) with more data following
        - Timestamp is unrealistic (suggests encrypted data, not real timestamp)
        - High entropy in the data

        Args:
            raw_bytes: Raw packet bytes
            sensor_readings: Decoded sensor readings (empty if encrypted)
            timestamp: Extracted timestamp value

        Returns:
            True if packet appears encrypted
        """
        # No sensors decoded
        if len(sensor_readings) == 0 and len(raw_bytes) > 5:
            # Check if first LPP byte is channel 0 with data following
            if raw_bytes[5] == 0x00 and len(raw_bytes) > 7:
                return True

            # Check for unrealistic timestamp (1970-2000 or > 2100)
            if timestamp < 946684800 or timestamp > 4102444800:  # Before 2000 or after 2100
                return True

            # High entropy check - encrypted data should have relatively uniform byte distribution
            if len(raw_bytes) > 10:
                # Count unique bytes in payload
                unique_bytes = len(set(raw_bytes[5:]))
                if unique_bytes > len(raw_bytes[5:]) * 0.6:  # High diversity suggests encryption
                    return True

        return False

    def decrypt_payload(self, encrypted_data: bytes, psk: Optional[bytes] = None) -> Optional[bytes]:
        """
        Decrypt an encrypted payload using MeshCore's encryption scheme

        MeshCore uses:
        - AES-128 ECB mode (no IV)
        - Zero-padding (not PKCS7) for partial blocks
        - Optional HMAC-SHA256 prefix (4 bytes) if using encryptThenMAC

        Structure options:
        1. [Encrypted Data] - plain AES-ECB encrypted
        2. [HMAC(4)] [Encrypted Data] - with authentication

        Args:
            encrypted_data: Encrypted payload bytes
            psk: Pre-shared key (uses self.psk if not provided)

        Returns:
            Decrypted bytes if successful, None otherwise
        """
        if not CRYPTO_AVAILABLE:
            return None

        psk = psk if psk is not None else self.psk

        # Try different offsets (the "raw" field may include headers)
        # Common offsets: 0 (no header), 4 (MAC), 9 (packet header), etc.
        for offset in [0, 4, 8, 9, 12, 16]:
            try:
                if offset >= len(encrypted_data):
                    continue

                # Skip offset bytes
                ciphertext = encrypted_data[offset:]

                # Ensure length is multiple of 16 (AES block size)
                if len(ciphertext) % 16 != 0:
                    continue

                # Decrypt using AES-ECB (matches MeshCore's encrypt() function)
                cipher = AES.new(psk, AES.MODE_ECB)
                decrypted = cipher.decrypt(ciphertext)

                # Remove zero-padding (MeshCore pads with zeros, not PKCS7)
                # Find the actual data length by looking for valid structure
                decrypted_trimmed = self._remove_zero_padding(decrypted)

                # Validate: check if decrypted data looks reasonable
                if self._is_valid_decrypted_data(decrypted_trimmed):
                    return decrypted_trimmed

                # Also try without trimming (in case there are legitimate zero bytes)
                if self._is_valid_decrypted_data(decrypted):
                    return decrypted

            except Exception as e:
                continue

        return None

    def _remove_zero_padding(self, data: bytes) -> bytes:
        """
        Remove zero-padding from decrypted data

        MeshCore zero-pads partial blocks, so we need to find where the
        actual data ends. We look for the end of valid LPP data.

        Args:
            data: Decrypted data with potential zero padding

        Returns:
            Data with zero padding removed
        """
        if len(data) < 5:
            return data

        # Start after the fixed header (timestamp + flags)
        min_len = 5
        pos = 5

        # Parse LPP data to find actual end
        while pos < len(data):
            channel = data[pos]

            # Channel 0 marks end of LPP data
            if channel == 0:
                # Everything after this should be padding
                return data[:pos + 1]

            if channel > 0x14:  # Invalid channel number
                # Likely hit padding or garbage
                break

            # Need at least 2 more bytes (channel + type)
            if pos + 2 > len(data):
                break

            data_type = data[pos + 1]

            # Get size for this type
            if data_type in CayenneLPPDecoder.TYPE_INFO:
                size, _, _, _, _ = CayenneLPPDecoder.TYPE_INFO[data_type]
                pos += 2 + size  # channel + type + value
            else:
                # Unknown type, can't continue parsing
                break

        # Return data up to where we stopped parsing
        return data[:pos] if pos > min_len else data

    def _is_valid_decrypted_data(self, data: bytes) -> bool:
        """
        Check if decrypted data looks valid

        Valid decrypted telemetry should have:
        - At least 5 bytes (timestamp + flags)
        - Reasonable timestamp
        - Valid LPP channel markers (0x00 end marker or 0x01-0x0F for channels)

        Args:
            data: Decrypted data to validate

        Returns:
            True if data appears valid
        """
        if len(data) < 5:
            return False

        # Check timestamp (bytes 0-3)
        timestamp = struct.unpack('>I', data[0:4])[0]
        # Reasonable timestamp: 2000-2100
        if not (946684800 <= timestamp <= 4102444800):
            return False

        # Check flags byte (should be 0x00)
        if data[4] != 0x00:
            return False

        # Check if we have LPP data
        if len(data) > 5:
            # First LPP byte should be a channel number (0x00-0x0F typical, or end marker)
            if data[5] > 0x14:  # Channel numbers should be reasonable
                return False

        return True

    def decode_mqtt_json(self, mqtt_json: str) -> Dict[str, Any]:
        """
        Decode complete MQTT JSON message

        Args:
            mqtt_json: JSON string from MQTT message

        Returns:
            Dictionary with packet metadata and decoded payload
        """
        try:
            data = json.loads(mqtt_json)
        except json.JSONDecodeError as e:
            return {"error": f"Invalid JSON: {e}"}

        # Decode the raw payload
        decoded_payload = {}
        if "raw" in data:
            decoded_payload = self.decode_raw_payload(data["raw"])

        return {
            "packet_info": {
                "origin": data.get("origin"),
                "origin_id": data.get("origin_id"),
                "timestamp": data.get("timestamp"),
                "direction": data.get("direction"),
                "packet_type": data.get("packet_type"),
                "route": data.get("route"),
            },
            "signal_info": {
                "SNR": data.get("SNR"),
                "RSSI": data.get("RSSI"),
                "score": data.get("score"),
                "duration": data.get("duration"),
            },
            "payload": decoded_payload,
            "hash": data.get("hash")
        }


def pretty_print_decoded(decoded: Dict[str, Any]) -> None:
    """Pretty print decoded packet data"""

    print("\n" + "="*70)
    print("MQTT PACKET DECODER - Sleepy Sensor")
    print("="*70)

    # Packet Info
    if "packet_info" in decoded:
        print("\n📦 PACKET INFO:")
        for key, value in decoded["packet_info"].items():
            if value:
                print(f"  {key:15s}: {value}")

    # Signal Info
    if "signal_info" in decoded:
        print("\n📡 SIGNAL INFO:")
        for key, value in decoded["signal_info"].items():
            if value:
                unit = ""
                if key == "RSSI":
                    unit = " dBm"
                elif key == "SNR":
                    unit = " dB"
                elif key == "duration":
                    unit = " ms"
                print(f"  {key:15s}: {value}{unit}")

    # Payload
    if "payload" in decoded:
        payload = decoded["payload"]
        print("\n📊 PAYLOAD:")

        if "error" in payload:
            print(f"  ❌ Error: {payload['error']}")
        else:
            # Show encryption warning prominently
            if payload.get("encrypted"):
                print("\n  ⚠️  WARNING: ENCRYPTED PACKET DETECTED!")
                print(f"  {payload.get('warning', '')}")
                print("\n  ℹ️  DECRYPTION INFO:")
                if "decryption_help" in payload:
                    help_info = payload["decryption_help"]
                    print(f"     Algorithm: {help_info.get('algorithm', 'Unknown')}")
                    print(f"     PSK Format: {help_info.get('psk_format', 'Unknown')}")
                    print(f"     How to decrypt: {help_info.get('how_to_decrypt', '')}")
                    print(f"     Device config: {help_info.get('device_config', '')}")
                print()

            print(f"  RTC Timestamp  : {payload.get('rtc_timestamp')} ({payload.get('rtc_datetime')})")
            print(f"  Flags          : {payload.get('flags')}")
            print(f"  Encrypted      : {payload.get('encrypted', False)}")

            if payload.get('decryption_attempted'):
                if payload.get('decryption_successful'):
                    print(f"  Decryption     : ✓ SUCCESS")
                else:
                    print(f"  Decryption     : ✗ FAILED")

            if payload.get('decryption_note'):
                print(f"  Note           : {payload.get('decryption_note')}")

            print(f"  Sensor Count   : {payload.get('sensor_count')}")

            if payload.get("sensors") and not payload.get("encrypted"):
                print("\n  🔬 SENSOR READINGS:")
                for i, sensor in enumerate(payload["sensors"], 1):
                    print(f"\n    [{i}] Channel {sensor['channel']}: {sensor['name']}")
                    print(f"        Type  : {sensor['type']}")

                    if isinstance(sensor['value'], dict):
                        print(f"        Value :")
                        for k, v in sensor['value'].items():
                            print(f"          {k}: {v}")
                    else:
                        unit_str = f" {sensor['unit']}" if sensor['unit'] else ""
                        print(f"        Value : {sensor['value']}{unit_str}")

    # Hash
    if "hash" in decoded and decoded["hash"]:
        print(f"\n🔐 Hash: {decoded['hash']}")

    print("\n" + "="*70 + "\n")


def main():
    """Example usage"""

    # Example MQTT JSON from user
    example_json = """{
    "origin": "GVA - Observer",
    "origin_id": "3DF38C372AF1AC2C273053771008635495D085C3BF779395E67611F56B1B6A73",
    "timestamp": "2025-12-13T11:57:47.605561",
    "type": "PACKET",
    "direction": "rx",
    "time": "07:55:22",
    "date": "23/5/2024",
    "len": "41",
    "packet_type": "6",
    "route": "F",
    "payload_len": "35",
    "raw": "18B92400000000920E807F3A6036D8C52BFC11408B38C8E409BCC790695A23A2DAA410B849D82BD52B",
    "SNR": "4",
    "RSSI": "-76",
    "score": "1000",
    "duration": "476",
    "hash": "BA8A9BC45F886D9E"
}"""

    # Create decoder and decode
    decoder = MQTTPacketDecoder()
    decoded = decoder.decode_mqtt_json(example_json)

    # Pretty print results
    pretty_print_decoded(decoded)

    # Also show raw JSON output for programmatic use
    print("\n📄 JSON OUTPUT (for programmatic use):")
    print(json.dumps(decoded, indent=2))


if __name__ == "__main__":
    main()
