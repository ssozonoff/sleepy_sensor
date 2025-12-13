#!/usr/bin/env python3
"""
MQTT Listener for Sleepy Sensor telemetry data
Listens to MQTT topics and decodes incoming sensor messages
"""

import sys
import json
import argparse
import ssl
from datetime import datetime

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("❌ paho-mqtt is not installed")
    print("\nInstall it with:")
    print("  pip3 install --user paho-mqtt")
    sys.exit(1)

from mqtt_decoder import MQTTPacketDecoder, pretty_print_decoded, CRYPTO_AVAILABLE


class SensorMQTTListener:
    """MQTT listener for Sleepy Sensor telemetry"""

    def __init__(self, broker: str, port: int = 1883, topic: str = "sleepy_sensor/#",
                 psk: bytes = None, username: str = None, password: str = None,
                 use_tls: bool = False, tls_insecure: bool = False,
                 ca_certs: str = None, certfile: str = None, keyfile: str = None,
                 verbose: bool = False):
        """
        Initialize MQTT listener

        Args:
            broker: MQTT broker hostname/IP
            port: MQTT broker port (default: 1883 for plain, 8883 for TLS)
            topic: MQTT topic to subscribe to (default: sleepy_sensor/#)
            psk: Pre-shared key for decryption (None = all-zeros public channel)
            username: MQTT username (optional)
            password: MQTT password (optional)
            use_tls: Enable TLS/SSL encryption
            tls_insecure: Disable certificate verification (for self-signed certs)
            ca_certs: Path to CA certificates file
            certfile: Path to client certificate file
            keyfile: Path to client key file
            verbose: Show verbose output
        """
        self.broker = broker
        self.port = port
        self.topic = topic
        self.username = username
        self.password = password
        self.use_tls = use_tls
        self.tls_insecure = tls_insecure
        self.verbose = verbose

        # Create decoder
        self.decoder = MQTTPacketDecoder(psk=psk)

        # Statistics
        self.packets_received = 0
        self.packets_decoded = 0
        self.packets_failed = 0
        self.start_time = datetime.now()

        # Create MQTT client
        self.client = mqtt.Client()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.on_disconnect = self.on_disconnect

        # Set authentication if provided
        if username and password:
            self.client.username_pw_set(username, password)

        # Configure TLS if enabled
        if use_tls:
            tls_kwargs = {}

            # Set CA certificates
            if ca_certs:
                tls_kwargs['ca_certs'] = ca_certs

            # Set client certificate and key
            if certfile:
                tls_kwargs['certfile'] = certfile
            if keyfile:
                tls_kwargs['keyfile'] = keyfile

            # Set TLS version (use highest available)
            tls_kwargs['tls_version'] = ssl.PROTOCOL_TLS

            # Configure certificate requirements
            if tls_insecure:
                tls_kwargs['cert_reqs'] = ssl.CERT_NONE
            else:
                tls_kwargs['cert_reqs'] = ssl.CERT_REQUIRED

            self.client.tls_set(**tls_kwargs)

            # Disable hostname verification if insecure mode
            if tls_insecure:
                self.client.tls_insecure_set(True)

    def on_connect(self, client, userdata, flags, rc):
        """Callback when connected to MQTT broker"""
        if rc == 0:
            print(f"✓ Connected to MQTT broker: {self.broker}:{self.port}")
            print(f"✓ Subscribing to topic: {self.topic}")
            client.subscribe(self.topic)
            print(f"✓ Waiting for messages... (Ctrl+C to stop)\n")
            print("="*70)
        else:
            print(f"❌ Connection failed with code {rc}")
            sys.exit(1)

    def on_disconnect(self, client, userdata, rc):
        """Callback when disconnected from MQTT broker"""
        if rc != 0:
            print(f"\n⚠️  Unexpected disconnect (code: {rc})")

    def on_message(self, client, userdata, msg):
        """Callback when a message is received"""
        self.packets_received += 1

        try:
            # Get message payload
            payload = msg.payload.decode('utf-8')

            if self.verbose:
                print(f"\n📨 Message received on topic: {msg.topic}")
                print(f"   Packet #{self.packets_received}")
                print(f"   Timestamp: {datetime.now().isoformat()}")

            # Try to parse as JSON
            try:
                mqtt_json = payload
                decoded = self.decoder.decode_mqtt_json(mqtt_json)

                # Pretty print the decoded message
                print()  # Blank line for separation
                pretty_print_decoded(decoded)

                # Check if decryption was successful
                if decoded.get("payload", {}).get("decryption_successful"):
                    self.packets_decoded += 1
                elif decoded.get("payload", {}).get("encrypted"):
                    self.packets_failed += 1
                    print("⚠️  Decryption failed - check PSK configuration")
                else:
                    self.packets_decoded += 1

                # Show statistics
                if self.verbose:
                    self.show_stats()

            except json.JSONDecodeError:
                print(f"⚠️  Non-JSON message on {msg.topic}: {payload[:100]}...")
                self.packets_failed += 1

        except Exception as e:
            print(f"❌ Error processing message: {e}")
            if self.verbose:
                import traceback
                traceback.print_exc()
            self.packets_failed += 1

    def show_stats(self):
        """Show statistics"""
        uptime = (datetime.now() - self.start_time).total_seconds()
        print(f"\n📊 Statistics:")
        print(f"   Uptime: {uptime:.0f}s")
        print(f"   Total received: {self.packets_received}")
        print(f"   Decoded: {self.packets_decoded}")
        print(f"   Failed: {self.packets_failed}")
        print("="*70)

    def run(self):
        """Start the MQTT listener"""
        try:
            print("\n" + "="*70)
            print("SLEEPY SENSOR MQTT LISTENER")
            print("="*70)
            print(f"Broker: {self.broker}:{self.port}")
            print(f"Topic: {self.topic}")
            print(f"TLS: {'Enabled' if self.use_tls else 'Disabled'}")
            if self.use_tls:
                print(f"Certificate verification: {'Disabled (insecure)' if self.tls_insecure else 'Enabled'}")
            print(f"PSK: {'Custom' if self.decoder.psk != MQTTPacketDecoder.PUBLIC_CHANNEL_PSK else 'Public (all-zeros)'}")
            print(f"Crypto available: {CRYPTO_AVAILABLE}")
            print("="*70 + "\n")

            # Connect to broker
            self.client.connect(self.broker, self.port, 60)

            # Start listening loop
            self.client.loop_forever()

        except KeyboardInterrupt:
            print("\n\n⏹️  Stopping...")
            self.show_stats()
            self.client.disconnect()
            print("✓ Disconnected")

        except Exception as e:
            print(f"\n❌ Error: {e}")
            sys.exit(1)


def main():
    """Main entry point"""

    parser = argparse.ArgumentParser(
        description="MQTT listener for Sleepy Sensor telemetry data",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Listen to default topic with public channel (all-zeros PSK)
  %(prog)s mqtt.example.com

  # Listen to specific topic
  %(prog)s mqtt.example.com -t "sensors/sleepy/#"

  # Use custom PSK (base64 encoded)
  %(prog)s mqtt.example.com -k "YWJjZGVmZ2hpamtsbW5vcA=="

  # With MQTT authentication
  %(prog)s mqtt.example.com -u username -p password

  # With TLS (default port 8883)
  %(prog)s mqtt.example.com --tls -P 8883

  # With TLS and no certificate verification (self-signed cert)
  %(prog)s mqtt.example.com --tls --tls-insecure -P 8883

  # With TLS and custom CA certificate
  %(prog)s mqtt.example.com --tls --ca-certs /path/to/ca.crt -P 8883

  # With client certificate authentication
  %(prog)s mqtt.example.com --tls --certfile client.crt --keyfile client.key -P 8883

  # Verbose output
  %(prog)s mqtt.example.com -v
        """
    )

    parser.add_argument("broker", help="MQTT broker hostname or IP address")
    parser.add_argument("-P", "--port", type=int, default=1883, help="MQTT broker port (default: 1883 for plain, 8883 for TLS)")
    parser.add_argument("-t", "--topic", default="sleepy_sensor/#", help="MQTT topic to subscribe to (default: sleepy_sensor/#)")
    parser.add_argument("-k", "--psk", help="Pre-shared key for decryption (base64 encoded). If not provided, uses all-zeros (public channel)")
    parser.add_argument("-u", "--username", help="MQTT username")
    parser.add_argument("-p", "--password", help="MQTT password")

    # TLS options
    tls_group = parser.add_argument_group('TLS/SSL options')
    tls_group.add_argument("--tls", action="store_true", help="Enable TLS/SSL encryption")
    tls_group.add_argument("--tls-insecure", action="store_true", help="Disable certificate verification (for self-signed certificates)")
    tls_group.add_argument("--ca-certs", help="Path to CA certificates file")
    tls_group.add_argument("--certfile", help="Path to client certificate file")
    tls_group.add_argument("--keyfile", help="Path to client key file")

    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")

    args = parser.parse_args()

    # Decode PSK if provided
    psk = None
    if args.psk:
        try:
            import base64
            psk = base64.b64decode(args.psk)
            if len(psk) not in [16, 32]:
                print(f"❌ PSK must be 16 or 32 bytes (got {len(psk)} bytes)")
                sys.exit(1)
            print(f"✓ Using custom PSK ({len(psk)} bytes)")
        except Exception as e:
            print(f"❌ Invalid PSK: {e}")
            sys.exit(1)

    # Warn if using TLS insecure mode
    if args.tls_insecure and not args.tls:
        print("⚠️  Warning: --tls-insecure requires --tls to be enabled")
        args.tls = True

    if args.tls_insecure:
        print("⚠️  Warning: Certificate verification is DISABLED. Connection is encrypted but not authenticated.")
        print("   This should only be used for testing with self-signed certificates.\n")

    # Create and run listener
    listener = SensorMQTTListener(
        broker=args.broker,
        port=args.port,
        topic=args.topic,
        psk=psk,
        username=args.username,
        password=args.password,
        use_tls=args.tls,
        tls_insecure=args.tls_insecure,
        ca_certs=args.ca_certs,
        certfile=args.certfile,
        keyfile=args.keyfile,
        verbose=args.verbose
    )

    listener.run()


if __name__ == "__main__":
    main()
