from mqtt_decoder import MQTTPacketDecoder, pretty_print_decoded

# Decode with public channel (all-zeros PSK)
decoder = MQTTPacketDecoder()

mqtt_json = ""

result = decoder.decode_mqtt_json(mqtt_json)
pretty_print_decoded(result)
