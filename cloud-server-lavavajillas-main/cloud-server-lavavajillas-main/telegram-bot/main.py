import json
import os
import requests
import paho.mqtt.client as mqtt
from datetime import datetime, timezone, timedelta

MQTT_BROKER = "mosquitto"
MQTT_PORT   = 1883

TELEGRAM_TOKEN   = os.environ["TELEGRAM_TOKEN"]
TELEGRAM_CHAT_ID = os.environ["TELEGRAM_CHAT_ID"]

TOPICS = [
    "cloud/+/events/access",
    "cloud/+/events/alarm",
]

SPAIN_TZ = timezone(timedelta(hours=2))

def send_telegram(message: str):
    url = f"https://api.telegram.org/bot{TELEGRAM_TOKEN}/sendMessage"
    try:
        r = requests.post(url, json={
            "chat_id": TELEGRAM_CHAT_ID,
            "text": message,
            "parse_mode": "HTML",
        }, timeout=10)
        print(f"[TELEGRAM] Sent: {r.status_code}", flush=True)
    except Exception as e:
        print(f"[TELEGRAM] Error: {e}", flush=True)

def format_ts(ts) -> str:
    try:
        dt_utc = datetime.fromtimestamp(int(ts), tz=timezone.utc)
        dt_spain = dt_utc.astimezone(SPAIN_TZ)
        return dt_spain.strftime("%Y-%m-%d %H:%M:%S")
    except Exception:
        return str(ts)

def on_connect(client, userdata, flags, reason_code, properties=None):
    print(f"[MQTT] Connected: {reason_code}", flush=True)
    for t in TOPICS:
        client.subscribe(t)
        print(f"[MQTT] Subscribed to {t}", flush=True)

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload.decode())
    except Exception as e:
        print(f"[ERROR] JSON decode: {e}", flush=True)
        return

    ts = format_ts(payload.get("ts", ""))

    if "events/access" in msg.topic:
        card_id = payload.get("card_id", "unknown")
        result  = payload.get("result", "unknown")
        icon    = "✅" if result == "AUTHORIZED" else "❌"
        text = (
            f"{icon} <b>Intento de acceso</b>\n"
            f"💳 Tarjeta: <code>{card_id}</code>\n"
            f"🔐 Resultado: <b>{result}</b>\n"
            f"🕐 Hora: <code>{ts}</code>"
        )
        send_telegram(text)

    elif "events/alarm" in msg.topic:
        cmd  = payload.get("cmd", "unknown")
        icon = "🚨" if cmd == "ALARM_ON" else "✅"
        text = (
            f"{icon} <b>Alarma</b>\n"
            f"⚠️ Comando: <b>{cmd}</b>\n"
            f"🕐 Hora: <code>{ts}</code>"
        )
        send_telegram(text)

def main():
    print("[APP] Starting Telegram bot service...", flush=True)
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()

if __name__ == "__main__":
    main()
