#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// ── Configuración ─────────────────────────────────────────────────────────
const char* SSID             = "TU_SSID";
const char* PASSWORD         = "TU_PASSWORD";
const char* PROXY_IP         = "IP_DE_LA_PC";  // PC con servicio.py
const int   PROXY_PUERTO     = 5000;
const char* PATENTE_ESPERADA = "AA021ID";

#define IMG_BUF_SIZE (20 * 1024)
static uint8_t imgBuf[IMG_BUF_SIZE];
static size_t  imgLen = 0;

ESP8266WebServer server(80);

// ── Página web ────────────────────────────────────────────────────────────
const char HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Lector de Patentes</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 480px;
           margin: 50px auto; padding: 0 20px; background: #f0f0f0; }
    .card { background: white; border-radius: 10px; padding: 28px;
            box-shadow: 0 2px 8px rgba(0,0,0,.15); }
    h2 { margin-top: 0; color: #333; }
    input[type=file] { display: block; margin: 14px 0; width: 100%; }
    button { background: #0077cc; color: white; border: none;
             padding: 11px 28px; border-radius: 5px; cursor: pointer;
             font-size: 1em; width: 100%; }
    button:disabled { background: #aaa; cursor: default; }
    #resultado { margin-top: 20px; border-radius: 8px; padding: 18px;
                 text-align: center; display: none; }
    #resultado h1 { margin: 0 0 8px; font-size: 3em; }
    #resultado p  { margin: 4px 0; font-size: 1em; }
    #resultado small { opacity: .7; font-size: .8em; }
    .ok  { background: #d4edda; color: #155724; display: block !important; }
    .err { background: #f8d7da; color: #721c24; display: block !important; }
    .spin { color: #888; font-size: .9em; margin-top: 12px; display: none; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Lector de Patentes</h2>
    <p>Subí una foto de la patente (JPEG, max 20 KB).</p>
    <form id="frm">
      <input type="file" id="img" accept="image/jpeg" required>
      <button type="submit" id="btn">Reconocer patente</button>
    </form>
    <div class="spin" id="spin">Procesando...</div>
    <div id="resultado"></div>
  </div>
  <script>
    const frm  = document.getElementById('frm');
    const btn  = document.getElementById('btn');
    const spin = document.getElementById('spin');
    const res  = document.getElementById('resultado');

    frm.onsubmit = async (e) => {
      e.preventDefault();
      const file = document.getElementById('img').files[0];
      if (!file) return;

      btn.disabled = true;
      spin.style.display = 'block';
      res.className = '';
      res.style.display = 'none';

      try {
        const fd = new FormData();
        fd.append('imagen', file, file.name);
        const r = await fetch('/upload', { method: 'POST', body: fd });
        const j = await r.json();

        res.className = j.resultado === 'OK' ? 'ok' : 'err';
        const placa = j.patente ? `<p>Patente: <b>${j.patente}</b></p>` : '';
        const dbg   = j.debug   ? `<small>${j.debug}</small>` : '';
        res.innerHTML = `<h1>${j.resultado}</h1>${placa}${dbg}`;
      } catch (ex) {
        res.className = 'err';
        res.innerHTML = `<h1>ERROR</h1><p>${ex.message}</p>`;
      } finally {
        btn.disabled = false;
        spin.style.display = 'none';
      }
    };
  </script>
</body>
</html>
)HTML";

// ── Enviar imagen al proxy (servicio.py en la PC) ─────────────────────────
String llamarProxy() {
    WiFiClient wifi;
    HTTPClient http;

    String url = "http://" + String(PROXY_IP) + ":" + String(PROXY_PUERTO) + "/validar";
    Serial.println("[API] POST → " + url);
    Serial.printf("[API] Heap libre: %d bytes | imagen: %zu bytes\n",
                  ESP.getFreeHeap(), imgLen);

    http.begin(wifi, url);
    http.addHeader("Content-Type", "image/jpeg");
    http.setTimeout(20000);

    int code = http.POST(imgBuf, imgLen);
    String body = "";
    if (code > 0) {
        body = http.getString();
        Serial.printf("[API] HTTP %d: %s\n", code, body.c_str());
    } else {
        Serial.printf("[API] Error: %s\n", http.errorToString(code).c_str());
    }
    http.end();
    return body;
}

// ── Handlers ─────────────────────────────────────────────────────────────
void onUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        imgLen = 0;
        Serial.printf("[CAM] Recibiendo: %s\n", upload.filename.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (imgLen + upload.currentSize < IMG_BUF_SIZE)
            memcpy(imgBuf + imgLen, upload.buf, upload.currentSize), imgLen += upload.currentSize;
        else
            Serial.println("[CAM] Imagen demasiado grande (max 20 KB)");
    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("[CAM] Lista: %zu bytes\n", imgLen);
    }
}

void onUploadDone() {
    if (imgLen == 0) {
        server.send(400, "application/json", "{\"error\":\"imagen vacia\"}");
        return;
    }

    String body = llamarProxy();
    String respJson;

    if (body.isEmpty()) {
        respJson = "{\"resultado\":\"ERROR\","
                   "\"debug\":\"Sin respuesta del proxy " + String(PROXY_IP) + ":" + String(PROXY_PUERTO) + "\"}";
    } else {
        // El proxy devuelve {patente, resultado, debug} — solo sobreescribimos resultado
        DynamicJsonDocument doc(512);
        if (deserializeJson(doc, body) == DeserializationError::Ok) {
            String patente = doc["patente"] | String("");
            patente.toUpperCase();
            bool ok = (patente == String(PATENTE_ESPERADA));
            doc["resultado"] = ok ? "OK" : "ERROR";
            doc["debug"] = ok ? "Coincide"
                              : (patente.isEmpty() ? doc["debug"].as<String>()
                                                   : "No coincide (esperada: " + String(PATENTE_ESPERADA) + ")");
            serializeJson(doc, respJson);
        } else {
            respJson = body;
        }
    }

    server.send(200, "application/json", respJson);
    imgLen = 0;
}

// ── Setup / Loop ──────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    Serial.print("[WiFi] Conectando a ");
    Serial.println(SSID);
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Conectado. Abrir: http://%s\n",
                  WiFi.localIP().toString().c_str());

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", HTML);
    });
    server.on("/upload", HTTP_POST, onUploadDone, onUpload);

    server.begin();
    Serial.println("[HTTP] Servidor listo.");
}

void loop() {
    server.handleClient();
}
