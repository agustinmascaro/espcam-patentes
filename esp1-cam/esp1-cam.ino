#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ── Configuración ─────────────────────────────────────────────────────────
const char* SSID             = "Campus-TICs";
const char* API_TOKEN        = "TU_TOKEN_AQUI";   // platerecognizer.com
const char* PATENTE_ESPERADA = "AA021ID";

// 20 KB para imagen — dejamos ~30 KB libres para el stack SSL
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
    #resultado p  { margin: 0; font-size: 1em; color: #555; }
    .ok  { background: #d4edda; color: #155724; display: block !important; }
    .err { background: #f8d7da; color: #721c24; display: block !important; }
    .spin { color: #888; font-size: .9em; margin-top: 12px; display: none; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Lector de Patentes</h2>
    <p>Subí una foto de la patente (JPEG &lt; 20 KB).</p>
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
        const r   = await fetch('/upload', { method: 'POST', body: fd });
        const j   = await r.json();

        res.className = j.resultado === 'OK' ? 'ok' : 'err';
        res.innerHTML = `<h1>${j.resultado}</h1><p>Patente detectada: <b>${j.patente || '?'}</b></p>`;
        if (j.error) res.innerHTML = `<h1>ERROR</h1><p>${j.error}</p>`;
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

// ── Llamada a Plate Recognizer via HTTPS streaming ────────────────────────
// Enviamos la imagen en chunks para no necesitar un segundo buffer de copia.
String llamarPlateRecognizer() {
    WiFiClientSecure client;
    client.setInsecure();   // sin verificación de certificado (demo)

    Serial.println("[API] Conectando a platerecognizer.com...");
    if (!client.connect("api.platerecognizer.com", 443)) {
        Serial.println("[API] Fallo de conexion");
        return "";
    }

    String boundary   = "ESPBoundary";
    String partHeader = "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"upload\"; filename=\"p.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    String partFooter = "\r\n--" + boundary + "--\r\n";
    int contentLen = partHeader.length() + (int)imgLen + partFooter.length();

    // Cabeceras HTTP
    client.print("POST /v1/plate-reader/ HTTP/1.1\r\n");
    client.print("Host: api.platerecognizer.com\r\n");
    client.print("Authorization: Token "); client.print(API_TOKEN); client.print("\r\n");
    client.print("Content-Type: multipart/form-data; boundary="); client.print(boundary); client.print("\r\n");
    client.print("Content-Length: "); client.print(contentLen); client.print("\r\n");
    client.print("Connection: close\r\n\r\n");

    // Cuerpo multipart: header de parte + imagen en chunks + footer
    client.print(partHeader);
    const size_t CHUNK = 512;
    for (size_t i = 0; i < imgLen; i += CHUNK) {
        size_t n = min(CHUNK, imgLen - i);
        client.write(imgBuf + i, n);
        yield();    // evitar reset por watchdog
    }
    client.print(partFooter);

    // Leer respuesta: saltear cabeceras HTTP, quedarnos con el body JSON
    String body = "";
    bool  headersDone = false;
    unsigned long t = millis();
    while (client.connected() && millis() - t < 10000) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            if (!headersDone) {
                if (line == "\r") headersDone = true;
            } else {
                body += line;
            }
            t = millis();
        }
        yield();
    }
    client.stop();
    Serial.println("[API] Respuesta: " + body);
    return body;
}

// ── Extraer patente del JSON de respuesta ─────────────────────────────────
String extraerPatente(const String& json) {
    if (json.isEmpty()) return "";
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, json) != DeserializationError::Ok) return "";
    JsonArray results = doc["results"];
    if (results.size() == 0) return "";
    String p = results[0]["plate"].as<String>();
    p.toUpperCase();
    return p;
}

// ── Handlers de subida de imagen ──────────────────────────────────────────
void onUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        imgLen = 0;
        Serial.printf("[CAM] Recibiendo: %s\n", upload.filename.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (imgLen + upload.currentSize < IMG_BUF_SIZE) {
            memcpy(imgBuf + imgLen, upload.buf, upload.currentSize);
            imgLen += upload.currentSize;
        } else {
            Serial.println("[CAM] Imagen demasiado grande (max 20 KB)");
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("[CAM] Imagen lista: %zu bytes\n", imgLen);
    }
}

void onUploadDone() {
    if (imgLen == 0) {
        server.send(400, "application/json", "{\"error\":\"imagen vacia\"}");
        return;
    }

    String json    = llamarPlateRecognizer();
    String patente = extraerPatente(json);

    String resultado;
    if (patente.isEmpty()) {
        resultado = "{\"patente\":\"\",\"resultado\":\"ERROR\","
                    "\"error\":\"No se pudo contactar la API o no se detecto patente\"}";
    } else {
        bool ok = (patente == String(PATENTE_ESPERADA));
        resultado = "{\"patente\":\"" + patente + "\","
                    "\"resultado\":\"" + (ok ? "OK" : "ERROR") + "\"}";
        Serial.printf("[CAM] Patente: %s → %s\n", patente.c_str(), ok ? "OK" : "ERROR");
    }

    server.send(200, "application/json", resultado);
    imgLen = 0;
}

// ── Setup / Loop ──────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    Serial.print("[WiFi] Conectando a ");
    Serial.println(SSID);
    WiFi.begin(SSID);
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
