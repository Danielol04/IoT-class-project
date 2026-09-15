#include <Arduino.h>
#include <WiFi.h>
#include "time.h"
#include <DHT.h>
#include <Preferences.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configuración WiFi y MQTT ---
const char* ssid = "lavavajillas ";
const char* password = "lavavajillas";
const char* rpiLocal_IP = "10.48.210.182";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// --- Pines y Topics ---
#define DHTPIN 19
#define DHTTYPE DHT11 
#define LED_ROJO 23 
#define BuzzerPin 25

char topic_status[64], topic_imu[64], topic_ambiente[64], topic_alarma[64];

// --- Objetos y Variables Globales ---
Adafruit_MPU6050 mpu;
DHT dht(DHTPIN, DHTTYPE);
Preferences preferences;
WiFiClient espClient;
PubSubClient client(espClient);

String device_id = "";
unsigned int contadorGlobal = 0;

// Estructura para la Cola 
struct Mensaje_t {
    const char* topic;
    char* payload;
};

QueueHandle_t xColaMQTT;

// Semaforo para la alarma
SemaphoreHandle_t xSemaforoAlarma;
volatile bool alarmaActiva = false;

// --- Tareas ---
void vTareaVigilancia(void *pvParameters);
void vTareaAmbiente(void *pvParameters);
void vTareaConectividad(void *pvParameters);
void vTareaAlarma(void *pvParameters);
void vTareaSincronizarHora(void *pvParameters);
void vTareaStatus(void *pvParameters);


// --- Funciones ---

void conectarWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    static uint32_t ultimoIntento = 0;
    if (millis() - ultimoIntento < 10000) return; // Intentar solo cada 10 seg
    ultimoIntento = millis();

    Serial.println("WiFi: Intentando conectar...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

}
void callback(char* topic, byte* payload, unsigned int length) {
    // 1. Chivato para ver por qué canal te están hablando
    Serial.print("\n[MQTT] <<< Mensaje recibido en topic: ");
    Serial.println(topic);

    String mensaje = "";
    for (int i = 0; i < length; i++) mensaje += (char)payload[i];

    // 2. Chivato para ver el texto bruto que te llega
    Serial.print("[MQTT] <<< Contenido del mensaje: ");
    Serial.println(mensaje);

    if (mensaje.indexOf("ALARM_ON") != -1) {
        alarmaActiva = true;      
        xSemaphoreGive(xSemaforoAlarma);
        Serial.println("ACTIVANDO ALARMA");

    } else if (mensaje.indexOf("ALARM_OFF") != -1) {
        
        alarmaActiva = false;
        xSemaphoreGive(xSemaforoAlarma);
        Serial.println("DESACTIVANDO ALARMA");
        
    }
}

void reconectarMQTT() {
    if (client.connected()) return;

    if (WiFi.status() != WL_CONNECTED) {
        return; 
    }
    
    if (client.connect("ESP32_Nodo2", "lavavajillas", "lavavajillas")) { // id, user, pasword
        Serial.println("MQTT CONECTADO. Subscribiendose a los canales");
        // Comprobamos si la suscripción tiene éxito
        if (client.subscribe(topic_alarma)) {
            Serial.print("[ÉXITO] Suscrito correctamente a: ");
            Serial.println(topic_alarma);
        } else {
            Serial.println("[ERROR] No se pudo suscribir al canal.");
        }
    } else {
        Serial.print("Fallo, rc=");
        Serial.println(client.state());
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
// --- SETUP ---
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_ROJO, OUTPUT);
    pinMode(BuzzerPin, OUTPUT);
    
    device_id = WiFi.macAddress();
    conectarWiFi();
    delay(2000);

    // Memoria Persistente
    preferences.begin("sistema", false);
    unsigned int contador = preferences.getUInt("contador", 0);
    contadorGlobal = ++contador;
    preferences.putUInt("contador", contador);
    preferences.end();

    //RCN refactoriuzar esto lo hace mas legible
    // Inicialización Sensoresf
    dht.begin();
    if (!mpu.begin()) {
        while (1) delay(10);
    }
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setFilterBandwidth(MPU6050_BAND_10_HZ);

    // Configuración MQTT
    client.setServer(rpiLocal_IP, 1883);
    client.setCallback(callback);
    client.setBufferSize(3072);

    snprintf(topic_imu, 64, "%s/%s", "edge/telemetry/imu", device_id.c_str());
    snprintf(topic_status, 64, "%s/%s", "edge/status", device_id.c_str());
    snprintf(topic_ambiente, 64, "%s/%s", "edge/telemetry/temperatura_humidity", device_id.c_str());
    snprintf(topic_alarma, 64, "%s/%s", "edge/cmd/alarm", device_id.c_str());
    Serial.printf("Topics: \n%s\n%s\n%s\n%s\n" ,topic_status, topic_imu, topic_ambiente, topic_alarma);


    xColaMQTT = xQueueCreate(10, sizeof(Mensaje_t));
    xSemaforoAlarma = xSemaphoreCreateBinary();

    // Creación de Tareas
    xTaskCreate(vTareaAlarma, "Alarma", 1024, NULL, 2, NULL);
    xTaskCreate(vTareaVigilancia, "Vigilancia", 4096, NULL, 4, NULL);
    xTaskCreate(vTareaAmbiente,   "Ambiente",   4096, NULL, 2, NULL);
    xTaskCreate(vTareaSincronizarHora, "SincronizarHora", 3072, NULL, 1, NULL);
    xTaskCreate(vTareaConectividad, "Conectividad", 8192, NULL, 3, NULL);
    xTaskCreate(vTareaStatus, "Status", 3072, NULL, 1, NULL);

    //RCN no se cimprueba que se hayn creado bien. 
}
void vTareaVigilancia(void *pvParameters) {
    sensors_event_t a, g, temp;
    for (;;) {
        JsonDocument doc;
        doc["device_id"] = device_id;
        doc["start_ts"] = (uint32_t)time(NULL);
        doc["sample_period_s"] = 1; // Muestras cada 1 s
        JsonArray samples = doc["samples"].to<JsonArray>();

        // Datos durante 20 segundos
        for (int i = 0; i < 20; i++) {
            mpu.getEvent(&a, &g, &temp);
            JsonObject s = samples.add<JsonObject>();
            s["ax"] = a.acceleration.x; s["ay"] = a.acceleration.y; s["az"] = a.acceleration.z;
            s["gx"] = g.gyro.x;         s["gy"] = g.gyro.y;         s["gz"] = g.gyro.z;
            vTaskDelay(pdMS_TO_TICKS(1000)); // Muestras cada 1s
        }

        size_t n = measureJson(doc) +1;
        char* jsonPtr = (char*)malloc(n);

        if(jsonPtr){
            serializeJson(doc, jsonPtr, n);

            Mensaje_t msg;
            msg.topic = topic_imu;
            msg.payload = jsonPtr;

            if(xQueueSend(xColaMQTT, &msg, portMAX_DELAY) != pdPASS){
                free(jsonPtr);
            }
        }
    }
}

void vTareaAlarma(void *pvParameters) {   
       for (;;) {
            if (xSemaphoreTake(xSemaforoAlarma, portMAX_DELAY) == pdPASS) {
                while(alarmaActiva) {
                    digitalWrite(LED_ROJO, HIGH);
                    digitalWrite(BuzzerPin, HIGH);
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    // 1s silencio
                    digitalWrite(BuzzerPin, LOW);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                digitalWrite(LED_ROJO, LOW);
                digitalWrite(BuzzerPin, LOW);
            }
        }
}



void vTareaAmbiente(void *pvParameters) {
    for (;;) {
        JsonDocument doc;
        doc["device_id"] = device_id;
        doc["start_ts"] = (uint32_t)time(NULL);
        doc["sample_period_s"] = 2; // Intervalo entre muestras

        JsonArray t_array = doc["temperature_samples"].to<JsonArray>();
        JsonArray h_array = doc["humidity_samples"].to<JsonArray>();

        // Muestra durante 20 seg
        for (int i = 0; i < 20; i++) {
            float h = dht.readHumidity();
            float t = dht.readTemperature();

            // Si la lectura falla, ponemos de valor 0
            // RCN al final no hcisteis lo que os comenté...
            t_array.add(isnan(t) ? 0.0 : t);
            h_array.add(isnan(h) ? 0.0 : h);

            vTaskDelay(pdMS_TO_TICKS(2000)); // Intervalo entre muestras
        }

        size_t n = measureJson(doc) +1;
        char* jsonPtr = (char*)malloc(n);

        if(jsonPtr){
            serializeJson(doc, jsonPtr, n);
            
            Mensaje_t msg;
            msg.topic = topic_ambiente;
            msg.payload = jsonPtr;

            if(xQueueSend(xColaMQTT, &msg, portMAX_DELAY) != pdPASS){
                free(jsonPtr);
            }
        }
    }
}

void vTareaConectividad(void *pvParameters) {
    Mensaje_t msg;
    for (;;) {
        // Mantener conexiones
        if (WiFi.status() != WL_CONNECTED){
            conectarWiFi();
            vTaskDelay(2000);
        }
        if (!client.connected()) reconectarMQTT();  
        client.loop();

        // Revisar si hay algo que enviar
        if (xQueueReceive(xColaMQTT, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (client.connected()) {
                client.publish(msg.topic, msg.payload);
            }
            free(msg.payload);
        }
    }
}
void vTareaStatus(void *pvParameters) {
    for (;;) {
        JsonDocument doc;
        doc["device_id"] = device_id;
        doc["ts"] = (uint32_t)time(NULL);
        doc["boot_count"] = contadorGlobal;
        doc["uptime_s"] = (uint32_t)(esp_timer_get_time() / 1000000);
        
        size_t n = measureJson(doc) +1;
        char* jsonPtr = (char*)malloc(n);

        if(jsonPtr){
            serializeJson(doc, jsonPtr, n);
            
            Mensaje_t msg;
            msg.topic = topic_status;
            msg.payload = jsonPtr;

            if(xQueueSend(xColaMQTT, &msg, portMAX_DELAY) != pdPASS){
                free(jsonPtr);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(60000)); // Cada 1 minuto
    }
}
void vTareaSincronizarHora(void *pvParameters) {
    configTime(gmtOffset_sec, daylightOffset_sec, rpiLocal_IP);

    for (;;) {
        struct tm timeinfo;
        
        if (getLocalTime(&timeinfo, 1000)) {
            Serial.print("NTP [OK]: ");
            Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");

            vTaskDelay(pdMS_TO_TICKS(60000)); // 1 minuto
        } else {
            Serial.println("NTP [ERROR]: No se pudo sincronizar. Reintentando en 10s...");
            vTaskDelay(pdMS_TO_TICKS(10000)); // 10 seg
        }
    }
}

void loop() {}