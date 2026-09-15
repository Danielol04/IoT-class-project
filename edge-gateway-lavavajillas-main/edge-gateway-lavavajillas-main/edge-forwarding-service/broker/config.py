import os

MQTT_LOCAL_BROKER = os.getenv("MQTT_LOCAL_BROKER", "localhost")
MQTT_GLOBAL_BROKER = os.getenv("MQTT_GLOBAL_BROKER", "")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))

MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")


CLIENT_ID_RECEIVER = "Edge_Forwarder_Lister"
CLIENT_ID_SENDER = "Edge_Forwarder_Pusher"

TOPIC_IMU = "edge/telemetry/imu/FC:B4:67:56:51:6C"
TOPIC_DHT = "edge/telemetry/temperatura_humidity/FC:B4:67:56:51:6C"

TOPIC_NODE1_STATUS="edge/status/C0:49:EF:CA:65:A0"
TOPIC_NODE1_ACCESS = "edge/cmd/access/C0:49:EF:CA:65:A0"

TOPIC_NODE2_STATUS="edge/status/FC:B4:67:56:51:6C"
TOPIC_NODE2_ALARM = "edge/cmd/alarm/FC:B4:67:56:51:6C"

TOPIC_CLOUD_STATUS_NODE1="cloud/lavavajillas/status/C0:49:EF:CA:65:A0"
TOPIC_CLOUD_STATUS_NODE2="cloud/lavavajillas/status/FC:B4:67:56:51:6C"
TOPIC_CLOUD_ACCESS = "cloud/lavavajillas/events/access"
TOPIC_CLOUD_IMU = "cloud/lavavajillas/telemetry/imu/FC:B4:67:56:51:6C"
TOPIC_CLOUD_DHT = "cloud/lavavajillas/telemetry/temperature_humidity/FC:B4:67:56:51:6C"
TOPIC_CLOUD_ALARM = "cloud/lavavajillas/events/alarm"