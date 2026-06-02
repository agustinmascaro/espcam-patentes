#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ── Configuración ─────────────────────────────────────────────────────────
const char* SSID = "Campus-TICs";  // red abierta, sin contraseña

// GPIO donde está conectado el módulo de relé.
// La mayoría de módulos de relé son activo-BAJO: LOW = bobina energizada.
const int  PIN_RELE         = D1;
const bool RELE_ACTIVO_BAJO = true;

ESP8266WebServer server(80);
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

// ── Setup / Loop ───────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(PIN_RELE, OUTPUT);
    setRele(false);  // seguro al arrancar

    Serial.print("[WiFi] Conectando a ");
    Serial.println(SSID);
    WiFi.begin(SSID);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Conectado. IP: http://%s\n",
                  WiFi.localIP().toString().c_str());

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

    server.begin();
    Serial.printf("[HTTP] ESP2 listo — relé en pin %d\n", PIN_RELE);
}

void loop() {
    server.handleClient();
}
