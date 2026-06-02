#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

// ── Configuración WiFi ────────────────────────────────────────────────────
const char* SSID = "Campus-TICs";  // red abierta, sin contraseña

ESP8266WebServer server(80);

// Buffer para la imagen recibida desde el navegador.
// ESP8266 tiene ~80 KB de heap libre; dejamos margen para el stack/WiFi.
#define IMG_BUF_SIZE (40 * 1024)
static uint8_t imgBuf[IMG_BUF_SIZE];
static size_t  imgLen = 0;

// ── Página web ────────────────────────────────────────────────────────────
const char HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP-CAM Simulador</title>
  <style>
    body { font-family: Arial, sans-serif; max-width: 480px;
           margin: 50px auto; padding: 0 20px; background: #f5f5f5; }
    h2   { color: #333; }
    .card { background: white; border-radius: 8px; padding: 24px;
            box-shadow: 0 2px 6px rgba(0,0,0,.12); }
    input[type=file] { display: block; margin: 16px 0; width: 100%; }
    button { background: #0077cc; color: white; border: none;
             padding: 10px 24px; border-radius: 4px; cursor: pointer; font-size: 1em; }
    button:disabled { background: #aaa; cursor: default; }
    #status { margin-top: 16px; padding: 10px; border-radius: 4px;
              font-size: .9em; white-space: pre-wrap; display: none; }
    .ok  { background: #d4edda; color: #155724; display: block !important; }
    .err { background: #f8d7da; color: #721c24; display: block !important; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Simulador ESP-CAM</h2>
    <p>Selecciona una imagen para simular una captura de camara.</p>
    <form id="frm">
      <input type="file" id="img" accept="image/jpeg,image/png" required>
      <button type="submit" id="btn">Enviar imagen</button>
    </form>
    <div id="status"></div>
  </div>
  <script>
    const frm = document.getElementById('frm');
    const btn = document.getElementById('btn');
    const st  = document.getElementById('status');

    frm.onsubmit = async (e) => {
      e.preventDefault();
      const file = document.getElementById('img').files[0];
      if (!file) return;

      btn.disabled = true;
      btn.textContent = 'Enviando...';
      st.className = '';
      st.textContent = '';

      try {
        const fd = new FormData();
        fd.append('imagen', file, file.name);

        const r   = await fetch('/upload', { method: 'POST', body: fd });
        const txt = await r.text();
        st.className = r.ok ? 'ok' : 'err';
        try { st.textContent = JSON.stringify(JSON.parse(txt), null, 2); }
        catch { st.textContent = txt; }
      } catch (ex) {
        st.className = 'err';
        st.textContent = 'Error de red: ' + ex.message;
      } finally {
        btn.disabled = false;
        btn.textContent = 'Enviar imagen';
      }
    };
  </script>
</body>
</html>
)HTML";

// ── TODO: comunicación con el otro ESP ───────────────────────────────────
//
// Completar esta función para enviar la imagen al ESP receptor (ESP2).
// Parámetros:
//   buf  - puntero al buffer con los bytes JPEG de la imagen
//   len  - cantidad de bytes de la imagen
// Retorna true si el envío fue exitoso.
//
bool enviarAlOtroESP(const uint8_t* buf, size_t len) {
    // Implementar aquí la comunicación con el ESP2.
    //
    // Ejemplo con HTTP:
    //
    //   HTTPClient http;
    //   http.begin("http://<IP_ESP2>/imagen");
    //   http.addHeader("Content-Type", "image/jpeg");
    //   int code = http.POST((uint8_t*)buf, len);
    //   http.end();
    //   return (code == 200);

    return false;   // <- reemplazar por la implementación real
}
// ─────────────────────────────────────────────────────────────────────────

// Callback de recepción multipart (llamado mientras llegan los chunks)
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
            Serial.println("[CAM] Buffer lleno, imagen demasiado grande");
        }

    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("[CAM] Imagen completa: %zu bytes\n", imgLen);
    }
}

// Handler POST /upload — se ejecuta al terminar la subida
void onUploadDone() {
    if (imgLen == 0) {
        server.send(400, "application/json", "{\"error\":\"imagen vacia\"}");
        return;
    }

    bool ok = enviarAlOtroESP(imgBuf, imgLen);

    String json = "{\"bytes\":"   + String(imgLen)
                + ",\"enviado\":" + (ok ? "true" : "false") + "}";

    Serial.printf("[CAM] Enviado al ESP2: %s\n", ok ? "OK" : "PENDIENTE");
    server.send(200, "application/json", json);

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
