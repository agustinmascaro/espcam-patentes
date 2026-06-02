#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// ── Configuración ─────────────────────────────────────────────────────────
const char* SSID         = "TU_SSID";
const char* PASSWORD     = "TU_PASSWORD";
const char* SERVICIO_URL = "http://192.168.1.X:5000/validar";

WebServer server(80);

// Buffer para la imagen recibida.
// ESP32-CAM tiene PSRAM de 4 MB; si no hay PSRAM se limita a ~60 KB en RAM interna.
#define IMG_BUF_SIZE (200 * 1024)
static uint8_t* imgBuf   = nullptr;
static size_t   imgLen   = 0;
static char     imgNombre[64] = "";

// ── Recepción de imagen ────────────────────────────────────────────────────

void handleUpload() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        imgLen = 0;
        strlcpy(imgNombre, upload.filename.c_str(), sizeof(imgNombre));
        Serial.printf("[IMG] Recibiendo: %s\n", imgNombre);

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (imgBuf && (imgLen + upload.currentSize) < IMG_BUF_SIZE) {
            memcpy(imgBuf + imgLen, upload.buf, upload.currentSize);
            imgLen += upload.currentSize;
        } else if (!imgBuf) {
            Serial.println("[IMG] Buffer no disponible");
        }

    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("[IMG] Recibidos: %zu bytes\n", imgLen);
    }
}

// Acepta también envío como body raw (Content-Type: image/jpeg)
void handleImagenRaw() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Metodo no permitido");
        return;
    }

    // Si ya se llenó por multipart (handleUpload) usamos ese buffer;
    // si llegó como body plain lo copiamos aquí.
    if (imgLen == 0 && server.hasArg("plain")) {
        String body = server.arg("plain");
        if (imgBuf && body.length() < IMG_BUF_SIZE) {
            memcpy(imgBuf, body.c_str(), body.length());
            imgLen = body.length();
        }
    }

    // Guardar nombre de archivo del header opcional
    if (server.hasHeader("X-Filename")) {
        strlcpy(imgNombre, server.header("X-Filename").c_str(), sizeof(imgNombre));
    }

    if (!imgBuf || imgLen == 0) {
        server.send(400, "application/json", "{\"error\":\"imagen vacia\"}");
        return;
    }

    Serial.printf("[FWD] Reenviando %zu bytes a servicio...\n", imgLen);

    // Reenviar al servicio de validación
    HTTPClient http;
    http.begin(SERVICIO_URL);
    http.addHeader("Content-Type", "image/jpeg");
    if (imgNombre[0]) {
        http.addHeader("X-Filename", imgNombre);
    }

    int code = http.POST(imgBuf, imgLen);
    String respuesta;

    if (code > 0) {
        respuesta = http.getString();
        Serial.printf("[FWD] Respuesta %d: %s\n", code, respuesta.c_str());
    } else {
        respuesta = "{\"error\":\"servicio no disponible\"}";
        Serial.printf("[FWD] Error HTTP: %s\n", http.errorToString(code).c_str());
    }
    http.end();

    imgLen = 0;  // limpiar buffer
    server.send(200, "application/json", respuesta);
}

// ── Setup / Loop ───────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    // Intentar usar PSRAM si está disponible (ESP32-CAM tiene 4 MB)
    if (psramFound()) {
        imgBuf = (uint8_t*)ps_malloc(IMG_BUF_SIZE);
        Serial.printf("[PSRAM] Buffer de %d KB en PSRAM\n", IMG_BUF_SIZE / 1024);
    } else {
        imgBuf = (uint8_t*)malloc(IMG_BUF_SIZE);
        Serial.printf("[RAM] Buffer de %d KB en RAM interna\n", IMG_BUF_SIZE / 1024);
    }

    if (!imgBuf) {
        Serial.println("[ERROR] No hay memoria para el buffer de imagen");
    }

    Serial.print("[WiFi] Conectando");
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());

    // Colectar header X-Filename
    const char* headers[] = {"X-Filename"};
    server.collectHeaders(headers, 1);

    // Endpoint principal: acepta imagen como multipart o raw body
    server.on("/imagen", HTTP_POST, handleImagenRaw, handleUpload);

    // Health check
    server.on("/ping", HTTP_GET, []() {
        server.send(200, "application/json", "{\"ok\":true,\"rol\":\"esp1-cam\"}");
    });

    server.begin();
    Serial.println("[HTTP] ESP1 listo — esperando imágenes en :80/imagen");
}

void loop() {
    server.handleClient();
}
