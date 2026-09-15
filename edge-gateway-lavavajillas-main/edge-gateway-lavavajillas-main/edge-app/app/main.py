import json
import time
import paho.mqtt.client as mqtt
import psycopg2
from config import (
    MQTT_BROKER,
    MQTT_PORT,
    MQTT_USERNAME,
    MQTT_PASSWORD,
    CLOUD_MQTT_BROKER,
    CLOUD_MQTT_PORT,
    TOPIC_RFID,
    TOPIC_IMU,
    TOPIC_NODE1_ACCESS,
    TOPIC_NODE2_ALARM
)

last_rfid_ts = None
alarm_active = False

def on_connect(client, userdata, flags, reason_code, properties=None):
    print(f"[MQTT-LOCAL] Connected with code {reason_code}", flush=True)
    client.subscribe(TOPIC_RFID)
    client.subscribe(TOPIC_IMU)
    print(f"[MQTT-LOCAL] Subscribed to {TOPIC_RFID}, {TOPIC_IMU}", flush=True)

def check_access_db(card_id):
    try:
        conn = psycopg2.connect(
            host=CLOUD_MQTT_BROKER, 
            database="iot_cloud", 
            user="iot_admin", 
            password="iot_secret",
            port=5432
        )
        cur = conn.cursor()
        # Verificamos si la tarjeta existe y está activa
        query = "SELECT owner_name FROM authorized_cards WHERE card_id = %s AND is_active = TRUE"
        cur.execute(query, (card_id,))
        result = cur.fetchone()
        cur.close()
        conn.close()
        
        if result:
            print(f"[DB] Usuario reconocido: {result[0]}", flush=True)
            return True
        return False
    except Exception as e:
        print(f"[ERROR-DB] No se pudo conectar a PostgreSQL: {e}", flush=True)
        return False

def handle_rfid(local_client, payload):
    global last_rfid_ts, alarm_active

    print("[RFID] Evento recibido:", payload, flush=True)

    card_id = payload.get("card_id", "")
    ts = payload.get("ts", int(time.time()))
    last_rfid_ts = ts

    authorized = check_access_db(card_id)  

    if authorized:
        result = "AUTHORIZED"
        #Solo apagar si estaba activa
        if alarm_active:
            alarm_off_msg = {"ts": int(time.time()), "cmd": "ALARM_OFF"}
            local_client.publish(TOPIC_NODE2_ALARM, json.dumps(alarm_off_msg))
            alarm_active = False
            print("[ALARM] Alarma desactivada por acceso válido.", flush=True)
    else:
        result = "DENIED"
        print(f"[RFID] Acceso denegado para tarjeta: {card_id}", flush=True)

    response = {"ts": int(time.time()), "card_id": card_id, "result": result}
    local_client.publish(TOPIC_NODE1_ACCESS, json.dumps(response))


def movement_is_suspicious(samples):
    suspicious_count = 0
    for sample in samples:
        # Giroscopio
        g_total = abs(sample.get("gx", 0)) + abs(sample.get("gy", 0)) + abs(sample.get("gz", 0))
        # Acelerometro
        a_total = abs(sample.get("ax", 0)) + abs(sample.get("ay", 0)) + abs(sample.get("az", 0))

        # Umbrales
        if g_total > 1.5 or a_total > 17.0:
            suspicious_count += 1

    return suspicious_count >= 3


def handle_imu(local_client, payload):
    global last_rfid_ts, alarm_active

    print("[IMU] Batch recibido", flush=True)

    start_ts = payload.get("start_ts", int(time.time()))
    samples = payload.get("samples", [])

    if movement_is_suspicious(samples):
        # Comparamos el timestamp del IMU con el ultimo evento RFID
        is_authorized_entry = (last_rfid_ts is not None) and (abs(start_ts - last_rfid_ts) <= 10)

        if is_authorized_entry:
            print("[IMU] Movimiento detectado pero ignorado (entrada autorizada).", flush=True)
            return

        # Si hay movomiento sospechoso pero no ha ocurrido un evento de entrada
        if not alarm_active:
            alarm_msg = {"ts": int(time.time()), "cmd": "ALARM_ON"}
            local_client.publish(TOPIC_NODE2_ALARM, json.dumps(alarm_msg))
            alarm_active = True
            print("[ALARM] Intruso Detectado", flush=True)

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
    except Exception as e:
        print(f"[ERROR] Invalid JSON in {msg.topic}: {e}", flush=True)
        return

    print(f"[MQTT-LOCAL] Mensaje en {msg.topic}", flush=True)

    if msg.topic == TOPIC_RFID:
        handle_rfid(client, payload)
    elif msg.topic == TOPIC_IMU:
        handle_imu(client, payload)


def main():
    local_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    local_client.on_connect = on_connect
    local_client.on_message = on_message

    if MQTT_USERNAME:
        local_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

    print(f"[MQTT-LOCAL] Connecting to {MQTT_BROKER}:{MQTT_PORT}", flush=True)
    local_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    local_client.loop_forever()


if __name__ == "__main__":
    main()