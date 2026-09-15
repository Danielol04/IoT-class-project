import os

MQTT_BROKER = os.getenv("MQTT_BROKER", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")

CLOUD_MQTT_BROKER = os.getenv("CLOUD_MQTT_BROKER", "")
CLOUD_MQTT_PORT = int(os.getenv("CLOUD_MQTT_PORT", "1883"))

TOPIC_RFID = "edge/events/rfid/C0:49:EF:CA:65:A0"
TOPIC_IMU = "edge/telemetry/imu/FC:B4:67:56:51:6C"
TOPIC_DHT = "edge/telemetry/temperature_humidity/FC:B4:67:56:51:6C"

TOPIC_NODE1_STATUS="edge/status/C0:49:EF:CA:65:A0"
TOPIC_NODE1_ACCESS = "edge/cmd/access/C0:49:EF:CA:65:A0"

TOPIC_NODE2_STATUS="edge/status/FC:B4:67:56:51:6C"
TOPIC_NODE2_ALARM = "edge/cmd/alarm/FC:B4:67:56:51:6C"