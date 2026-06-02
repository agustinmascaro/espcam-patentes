#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ── Configuración ─────────────────────────────────────────────────────────
const char* SSID     = "TU_SSID";
const char* PASSWORD = "TU_PASSWORD";

// GPIO donde está el módulo de relé.
// La mayoría de módulos de relé son activo-BAJO: LOW = bobina energizada.
const int PIN_RELE = 26;
const bool RELE_ACTIVO_BAJO = true;   // true para módulos de relé comunes

WebServer server(80);
bool releEncendido = false;

// ── Helpers ────────────────────────────────────────────────────────────────

void setRele(bool encender) {
    releEncendido = encender;
    bool nivel = RELE_ACTIVO_BAJO ? !encender : encender;
    digitalWrite(PIN_RELE, nivel ? HIGH : LOW);
    Serial.printf("[RELÉ] %s\n", encender ? "ACTIVADO" : "DESACTIVADO");
}

String estadoJson() {
    return "{\"rele\":\"" + String(releEncendido ? "on" : "off") + "\"}";
}

// ── Endpoints ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(PIN_RELE, OUTPUT);
    setRele(false);  // seguro al arrancar

    Serial.print("[WiFi] Conectando");
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());

    server.on("/rele/on", HTTP_GET, []() {
        setRele(true);
        server.send(200, "application/json", estadoJson());
    });

    server.on("/rele/off", HTTP_GET, []() {
        setRele(false);
        server.send(200, "application/json", estadoJson());
    });

    server.on("/estado", HTTP_GET, []() {
        server.send(200, "application/json", estadoJson());
    });

    server.on("/ping", HTTP_GET, []() {
        server.send(200, "application/json", "{\"ok\":true,\"rol\":\"esp2-rele\"}");
    });

    server.begin();
    Serial.printf("[HTTP] ESP2 listo — relé en GPIO %d, escuchando en :80\n", PIN_RELE);
}

void loop() {
    server.handleClient();
}
