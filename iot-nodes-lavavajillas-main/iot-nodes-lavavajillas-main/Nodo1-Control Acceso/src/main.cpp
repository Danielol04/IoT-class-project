#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <U8g2lib.h>
#include <PubSubClient.h>

//RCN lo ideal es poner tambien como macroconstantes los tiempos
// --- Configuración Pines ---
#define SS_PIN  5
#define LED_VERDE 26  // Led para Autorizado
#define LED_ROJO  27 // Led para Denegado

// --- Objetos ---
MFRC522DriverPinSimple ss_pin(SS_PIN);
MFRC522DriverSPI driver{ss_pin, SPI};
MFRC522 mfrc522{driver};
U8G2_SSD1306_64X48_ER_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// --- Estados del Sistema ---
enum EstadoNodo { ESPERA, LEYENDO, CONSULTANDO, AUTORIZADO, DENEGADO, ERROR_COMM };
EstadoNodo estadoActual = ESPERA;

// Variables Globales
Preferences preferences;
unsigned int contadorGlobal = 0;
String ultimoUID = "";
String device_id = "";

// --- Configuración WiFi ---
const char* ssid = "lavavajillas ";
const char* password = "lavavajillas";
const char* rpiLocal_IP = "10.48.210.181";

WiFiClient espClient;
PubSubClient client(espClient);

char topic_status[64], topic_rfid[64], topic_access[64];

struct Mensaje_t {
    const char* topic;
    char* payload;
};

QueueHandle_t xColaMQTT;
SemaphoreHandle_t xSemaforoEstadoPuerta;

void vTareaStatus(void *pvParameters);
void vTareaRFID(void *pvParameters);
void vTareaPantallaOLED(void *pvParameters);
void vTareaConectividad(void *pvParameters);
void vTareaSincronizarHora(void *pvParameters);

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
void reconectarMQTT() {
    if (client.connected()) return;

    if (WiFi.status() != WL_CONNECTED) {
        return; 
    }
    
    if (client.connect("ESP32_Nodo1", "lavavajillas", "lavavajillas")) { // id, user, pasword
        Serial.println("MQTT CONECTADO. Subscribiendose a los canales");
        client.subscribe(topic_access);
    } else {
        Serial.print("Fallo, rc=");
        Serial.println(client.state());
    }
}
void callback(char* topic, byte* payload, unsigned int length) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
        Serial.print("Error en deserialización: ");
        Serial.println(error.f_str());
        estadoActual = ERROR_COMM;
        xSemaphoreGive(xSemaforoEstadoPuerta);
        return;
    }

    const char* card_id_recibido = doc["card_id"];
    const char* result = doc["result"];

    if (card_id_recibido && result) {
        if (ultimoUID.equalsIgnoreCase(String(card_id_recibido))) {
            
            if (strcmp(result, "AUTHORIZED") == 0) {
                estadoActual = AUTORIZADO;
                Serial.println("Acceso: AUTORIZADO");
                xSemaphoreGive(xSemaforoEstadoPuerta);
            } 
            else if (strcmp(result, "DENIED") == 0) {
                estadoActual = DENEGADO;
                Serial.println("Acceso: DENEGADO");
                xSemaphoreGive(xSemaforoEstadoPuerta);
            } 
            else {
                estadoActual = ERROR_COMM;
                Serial.print("Resultado inesperado o error: ");
                Serial.println(result);
                xSemaphoreGive(xSemaforoEstadoPuerta);
            }
        } else {
            Serial.println("Aviso: ID de tarjeta no coincide con la última lectura.");
        }
    }
}
void setup() {
    Serial.begin(115200);
    pinMode(LED_VERDE, OUTPUT);
    pinMode(LED_ROJO, OUTPUT);

    device_id = WiFi.macAddress();
    conectarWiFi();
    delay(2000);
    
    // Preferencias y WiFi
    preferences.begin("sistema", false);
    unsigned int contador = preferences.getUInt("contador", 0);
    contadorGlobal = ++contador;
    preferences.putUInt("contador", contadorGlobal);
    preferences.end();

    // RCM limpia mucho el codigo refactorizar todas estas cosas e invocarlas desde el setup()
    // Pantalla OLED
    u8g2.begin();
    // RFID reader
    SPI.begin(18, 19, 23, SS_PIN); // SCK, MISO, MOSI, CS
    mfrc522.PCD_Init();
    // MQTT y Tasks
    client.setServer(rpiLocal_IP, 1883);
    client.setCallback(callback);

    snprintf(topic_status, 64, "%s/%s", "edge/status", device_id.c_str());
    snprintf(topic_rfid, 64, "%s/%s", "edge/events/rfid", device_id.c_str());
    snprintf(topic_access, 64, "%s/%s", "edge/cmd/access", device_id.c_str());
    Serial.printf("Topics: \n%s\n%s\n%s\n" ,topic_status, topic_rfid, topic_access);

    xSemaforoEstadoPuerta = xSemaphoreCreateBinary();
    xColaMQTT = xQueueCreate(10, sizeof(Mensaje_t));

    // CREACIÓN DE TAREAS
    xTaskCreate(vTareaRFID, "RFID_Logic", 4096, NULL, 3, NULL);
    xTaskCreate(vTareaPantallaOLED, "Display_LEDs", 2048, NULL, 2, NULL);
    xTaskCreate(vTareaConectividad, "Conectividad", 8192, NULL, 4, NULL);
    xTaskCreate(vTareaSincronizarHora, "SincronizarHora", 3072, NULL, 1, NULL);
    xTaskCreate(vTareaStatus, "Status", 3072, NULL, 1, NULL);

    xSemaphoreGive(xSemaforoEstadoPuerta);
}

// --- LÓGICA DE CONTROL Y RFID ---
void vTareaRFID(void *pvParameters) {
    for (;;) {
        if (estadoActual == ESPERA) {
            if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
                estadoActual = LEYENDO;
                xSemaphoreGive(xSemaforoEstadoPuerta);
                
                // Obtener UID
                ultimoUID = "";
                for (byte i = 0; i < mfrc522.uid.size; i++) {
                    ultimoUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
                    ultimoUID += String(mfrc522.uid.uidByte[i], HEX);
                }

                Serial.println("Evento RFID: " + ultimoUID);
                estadoActual = CONSULTANDO;
                xSemaphoreGive(xSemaforoEstadoPuerta);

                JsonDocument doc;
                doc["device_id"] = device_id;
                doc["ts"] = (uint32_t)time(NULL);
                doc["card_id"] = ultimoUID;
                size_t n = measureJson(doc) +1;
                char* jsonPtr = (char*)malloc(n);

                if(jsonPtr){
                    serializeJson(doc, jsonPtr, n);
                    
                    Mensaje_t msg;
                    msg.topic = topic_rfid;
                    msg.payload = jsonPtr;

                    if(xQueueSend(xColaMQTT, &msg, portMAX_DELAY) != pdPASS){
                        free(jsonPtr);
                    }
                }
                mfrc522.PICC_HaltA();
                mfrc522.PCD_StopCrypto1();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// --- GESTIÓN DE PANTALLA Y LEDS ---
void vTareaPantallaOLED(void *pvParameters) {
    for (;;) {
        if (xSemaphoreTake(xSemaforoEstadoPuerta, pdMS_TO_TICKS(2000)) == pdPASS) {
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_profont11_mf);

            switch (estadoActual) {
                case ESPERA:
                    u8g2.drawStr(0, 15, "Aproxime");
                    u8g2.drawStr(0, 30, "tarjeta");
                    digitalWrite(LED_VERDE, LOW);
                    digitalWrite(LED_ROJO, HIGH);
                    break;

                case LEYENDO:
                    u8g2.drawStr(0, 20, "Leyendo...");
                    break;

                case CONSULTANDO:
                    u8g2.drawStr(0, 15, "Consultando");
                    u8g2.drawStr(0, 30, "permisos...");
                    break;

                case AUTORIZADO:
                    u8g2.drawStr(0, 20, "ACCESO OK");
                    digitalWrite(LED_VERDE, HIGH);
                    digitalWrite(LED_ROJO, LOW);
                    u8g2.sendBuffer(); 
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    estadoActual = ESPERA;
                    xSemaphoreGive(xSemaforoEstadoPuerta);
                    break;

                case DENEGADO:
                    u8g2.drawStr(0, 20, "DENEGADO");
                    digitalWrite(LED_VERDE, LOW);
                    digitalWrite(LED_ROJO, HIGH);
                    u8g2.sendBuffer();
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    estadoActual = ESPERA;
                    xSemaphoreGive(xSemaforoEstadoPuerta);
                    break;

                case ERROR_COMM:
                    u8g2.drawStr(0, 20, "Error Comm");
                    u8g2.sendBuffer();
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    estadoActual = ESPERA;
                    xSemaphoreGive(xSemaforoEstadoPuerta);
                    break;
            }
            u8g2.sendBuffer();
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
    configTime(3600, 3600, (rpiLocal_IP));

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
void loop() {}