import json
import paho.mqtt.client as mqtt
import psycopg2

MQTT_BROKER = "mosquitto"
MQTT_PORT = 1883

TOPICS = [
    "cloud/+/events/access",
    "cloud/+/events/alarm"
]

SITE_ID_DEFAULT = "lavavajillas"

conn = psycopg2.connect(
    dbname="iot_cloud",
    user="iot_admin",
    password="iot_secret",
    host="postgres"
)

cur = conn.cursor()


def extract_site_id(topic: str) -> str:
    parts = topic.split("/")
    if len(parts) >= 2:
        return parts[1]
    return SITE_ID_DEFAULT


def on_connect(client, userdata, flags, reason_code, properties=None):
    print(f"[MQTT] Connected with reason_code={reason_code}", flush=True)
    for t in TOPICS:
        client.subscribe(t)
        print(f"[MQTT] Subscribed to {t}", flush=True)


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
    except Exception as e:
        print(f"[ERROR] JSON decode failed on topic {msg.topic}: {e}", flush=True)
        return

    topic = msg.topic
    site_id = extract_site_id(topic)

    print(f"[MQTT] Message received on {topic}: {payload}", flush=True)

    try:
        if "events/access" in topic:
            cur.execute(
                """
                INSERT INTO access_log (card_id, device_id, site_id, result, event_ts)
                VALUES (%s, %s, %s, %s, %s)
                """,
                (
                    payload.get("card_id"),
                    payload.get("device_id"),
                    site_id,
                    payload.get("result"),
                    payload.get("ts"),
                )
            )
            conn.commit()
            print("[DB] Inserted into access_log", flush=True)

        elif "events/alarm" in topic:
            alarm_type = payload.get("alarm_type") or payload.get("cmd") or "ALARM"
            cur.execute(
                """
                INSERT INTO alarm_log (device_id, site_id, alarm_type, event_ts, details)
                VALUES (%s, %s, %s, %s, %s)
                """,
                (
                    payload.get("device_id"),
                    site_id,
                    alarm_type,
                    payload.get("ts"),
                    json.dumps(payload),
                )
            )
            conn.commit()
            print("[DB] Inserted into alarm_log", flush=True)

    except Exception as e:
        conn.rollback()
        print(f"[ERROR] DB insert failed: {e}", flush=True)


def main():
    print("[APP] Starting ingestion service...", flush=True)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[MQTT] Connecting to {MQTT_BROKER}:{MQTT_PORT}", flush=True)
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()


if __name__ == "__main__":
    main()
