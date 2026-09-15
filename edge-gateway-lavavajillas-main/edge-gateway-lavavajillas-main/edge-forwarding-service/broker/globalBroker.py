import paho.mqtt.client as mqtt
# Importamos los tópicos del config para saber qué escuchar
from config import (
    MQTT_LOCAL_BROKER, MQTT_GLOBAL_BROKER, MQTT_PORT, 
    MQTT_USERNAME, MQTT_PASSWORD, TOPIC_IMU, TOPIC_DHT, 
    TOPIC_NODE1_ACCESS, TOPIC_NODE2_ALARM, 
    TOPIC_NODE1_STATUS, TOPIC_NODE2_STATUS,
    TOPIC_CLOUD_ACCESS, TOPIC_CLOUD_IMU, 
    TOPIC_CLOUD_DHT, TOPIC_CLOUD_ALARM,
    TOPIC_CLOUD_STATUS_NODE1, TOPIC_CLOUD_STATUS_NODE2
)

# Definimos a qué tópico global va cada cosa local
    # Esto cumple con: "Toda la telemetría y eventos relevantes"
mapping = {
    TOPIC_IMU: TOPIC_CLOUD_IMU,
    TOPIC_DHT: TOPIC_CLOUD_DHT,
    TOPIC_NODE1_ACCESS: TOPIC_CLOUD_ACCESS,
    TOPIC_NODE2_ALARM: TOPIC_CLOUD_ALARM,
    TOPIC_NODE1_STATUS: TOPIC_CLOUD_STATUS_NODE1,
    TOPIC_NODE2_STATUS: TOPIC_CLOUD_STATUS_NODE2
}

def on_message_local(client, userdata, msg):
    

    target_topic = mapping.get(msg.topic)
    if target_topic:
        print(f"[FORWARDING] {msg.topic} -> {target_topic}")
        # Enviamos msg.payload directo para mantener el timestamp (ts) original
        cloud_client.publish(target_topic, msg.payload, qos=1)

# --- CLIENTE CLOUD (EMISOR) ---
cloud_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "Edge_Gateway_Sender")
if MQTT_USERNAME:
    cloud_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
cloud_client.connect(MQTT_GLOBAL_BROKER, MQTT_PORT)
cloud_client.loop_start()

# --- CLIENTE LOCAL (RECEPTOR) ---
local_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, "Edge_Gateway_Receiver")
local_client.on_message = on_message_local

def on_connect_local(client, userdata, flags, rc, props):
    print("[MQTT] Conectado al local. Suscribiendo a telemetría y eventos...")
    client.subscribe(TOPIC_IMU)
    client.subscribe(TOPIC_DHT)
    client.subscribe(TOPIC_NODE1_ACCESS)
    client.subscribe(TOPIC_NODE2_ALARM)
    client.subscribe(TOPIC_NODE1_STATUS)
    client.subscribe(TOPIC_NODE2_STATUS)

local_client.on_connect = on_connect_local
if MQTT_USERNAME:
    local_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
local_client.connect(MQTT_LOCAL_BROKER, MQTT_PORT)
local_client.loop_forever()