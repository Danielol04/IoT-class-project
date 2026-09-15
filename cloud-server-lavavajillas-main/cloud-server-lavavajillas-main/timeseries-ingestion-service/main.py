import json
from datetime import datetime, timezone, timedelta

import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

MQTT_BROKER = "mosquitto"
MQTT_PORT = 1883

INFLUX_URL = "http://influxdb:8086"
INFLUX_TOKEN = "my-super-secret-token"
INFLUX_ORG = "iot_org"
INFLUX_BUCKET = "iot_data"

TOPICS = [
    "cloud/+/status/+",
    "cloud/+/telemetry/imu/+",
    "cloud/+/telemetry/temperature_humidity/+"
]

client_influx = InfluxDBClient(
    url=INFLUX_URL,
    token=INFLUX_TOKEN,
    org=INFLUX_ORG
)
write_api = client_influx.write_api(write_options=SYNCHRONOUS)


def ts_to_datetime(ts_seconds: int) -> datetime:
    return datetime.fromtimestamp(ts_seconds, tz=timezone.utc)


def extract_site_id(topic: str) -> str:
    parts = topic.split("/")
    if len(parts) >= 2:
        return parts[1]
    return "unknown"


def on_connect(client, userdata, flags, reason_code, properties=None):
    print(f"[MQTT] Connected with reason_code={reason_code}", flush=True)
    for t in TOPICS:
        client.subscribe(t)
        print(f"[MQTT] Subscribed to {t}", flush=True)


def handle_status(topic: str, payload: dict):
    site_id = extract_site_id(topic)
    device_id = payload.get("device_id", "unknown")
    ts = payload.get("ts")

    if ts is None:
        print("[WARN] Status message without ts", flush=True)
        return

    point = (
        Point("node_status")
        .tag("site_id", site_id)
        .tag("device_id", device_id)
        .field("boot_count", int(payload.get("boot_count", 0)))
        .field("uptime_s", int(payload.get("uptime_s", 0)))
        .time(ts_to_datetime(ts), WritePrecision.S)
    )

    write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=point)
    print(f"[INFLUX] node_status written for {device_id}", flush=True)


def handle_imu(topic: str, payload: dict):
    site_id = extract_site_id(topic)
    device_id = payload.get("device_id", "unknown")
    start_ts = payload.get("start_ts")
    sample_period_s = payload.get("sample_period_s", 1)
    samples = payload.get("samples", [])

    if start_ts is None or not isinstance(samples, list):
        print("[WARN] Invalid IMU batch", flush=True)
        return

    points = []
    for i, sample in enumerate(samples):
        sample_ts = start_ts + i * sample_period_s

        point = (
            Point("imu")
            .tag("site_id", site_id)
            .tag("device_id", device_id)
            .field("ax", float(sample.get("ax", 0.0)))
            .field("ay", float(sample.get("ay", 0.0)))
            .field("az", float(sample.get("az", 0.0)))
            .field("gx", float(sample.get("gx", 0.0)))
            .field("gy", float(sample.get("gy", 0.0)))
            .field("gz", float(sample.get("gz", 0.0)))
            .time(ts_to_datetime(sample_ts), WritePrecision.S)
        )
        points.append(point)

    if points:
        write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=points)
        print(f"[INFLUX] imu batch written: {len(points)} samples for {device_id}", flush=True)


def handle_dht(topic: str, payload: dict):
    site_id = extract_site_id(topic)
    device_id = payload.get("device_id", "unknown")
    start_ts = payload.get("start_ts")
    sample_period_s = payload.get("sample_period_s", 1)
    temperature_samples = payload.get("temperature_samples", [])
    humidity_samples = payload.get("humidity_samples", [])

    if start_ts is None or not isinstance(temperature_samples, list) or not isinstance(humidity_samples, list):
        print("[WARN] Invalid DHT batch", flush=True)
        return

    n = min(len(temperature_samples), len(humidity_samples))
    points = []

    for i in range(n):
        sample_ts = start_ts + i * sample_period_s

        point = (
            Point("temperature_humidity")
            .tag("site_id", site_id)
            .tag("device_id", device_id)
            .field("temperature", float(temperature_samples[i]))
            .field("humidity", float(humidity_samples[i]))
            .time(ts_to_datetime(sample_ts), WritePrecision.S)
        )
        points.append(point)

    if points:
        write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=points)
        print(f"[INFLUX] temperature_humidity batch written: {len(points)} samples for {device_id}", flush=True)


def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
    except Exception as e:
        print(f"[ERROR] JSON decode failed on {msg.topic}: {e}", flush=True)
        return

    print(f"[MQTT] Message received on {msg.topic}", flush=True)

    try:
        if "/status/" in msg.topic:
            handle_status(msg.topic, payload)
        elif "/telemetry/imu/" in msg.topic:
            handle_imu(msg.topic, payload)
        elif "/telemetry/temperature_humidity/" in msg.topic:
            handle_dht(msg.topic, payload)
    except Exception as e:
        print(f"[ERROR] Processing failed on {msg.topic}: {e}", flush=True)


def main():
    print("[APP] Starting timeseries ingestion service...", flush=True)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message

    print(f"[MQTT] Connecting to {MQTT_BROKER}:{MQTT_PORT}", flush=True)
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()


if __name__ == "__main__":
    main()
