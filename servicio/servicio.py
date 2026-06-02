#!/usr/bin/env python3
"""
Servicio de validación de patentes.
Recibe imágenes del ESP1, detecta la patente y comanda el relé del ESP2.

Variables de entorno:
    ESP2_IP     IP del ESP2 con relé  (default: 192.168.1.101)
    ESP2_PORT   Puerto del ESP2       (default: 80)
    RELE_DELAY  Segundos que el relé permanece activo (default: 5)
"""

import os
import threading
import time
from datetime import datetime
from pathlib import Path

from flask import Flask, request, jsonify
import requests

app = Flask(__name__)

ESP2_IP    = os.getenv("ESP2_IP",    "192.168.1.101")
ESP2_PORT  = int(os.getenv("ESP2_PORT", "80"))
RELE_DELAY = float(os.getenv("RELE_DELAY", "5"))

# Patentes habilitadas (en producción vendría de una BD)
patentes_autorizadas: set[str] = {"ABC123", "DEF456", "XYZ789"}

capturas_dir = Path(__file__).parent / "capturas"
capturas_dir.mkdir(exist_ok=True)


# ── OCR ──────────────────────────────────────────────────────────────────────

def leer_patente(imagen_bytes: bytes) -> str | None:
    """
    Intenta leer la patente de la imagen con easyocr.
    Si no está instalado, devuelve None y el servicio usa modo demo.
    """
    try:
        import easyocr
        import numpy as np
        from PIL import Image
        import io

        img = Image.open(io.BytesIO(imagen_bytes)).convert("RGB")
        arr = np.array(img)
        reader = easyocr.Reader(["en"], gpu=False, verbose=False)
        resultados = reader.readtext(arr, detail=0)
        # Buscar el texto que tenga formato de patente (letras+números)
        for texto in resultados:
            candidato = "".join(c for c in texto.upper() if c.isalnum())
            if 5 <= len(candidato) <= 8:
                return candidato
        return None
    except ImportError:
        return None


# ── Relé ─────────────────────────────────────────────────────────────────────

_rele_timer: threading.Timer | None = None

def _apagar_rele():
    try:
        requests.get(f"http://{ESP2_IP}:{ESP2_PORT}/rele/off", timeout=5)
        print(f"[RELÉ] Desactivado (timer {RELE_DELAY}s)")
    except Exception as e:
        print(f"[RELÉ] Error al desactivar: {e}")

def activar_rele_con_timer():
    global _rele_timer
    try:
        requests.get(f"http://{ESP2_IP}:{ESP2_PORT}/rele/on", timeout=5)
        print(f"[RELÉ] Activado por {RELE_DELAY}s")
    except Exception as e:
        print(f"[RELÉ] Error al activar: {e}")
        return False

    if _rele_timer and _rele_timer.is_alive():
        _rele_timer.cancel()
    _rele_timer = threading.Timer(RELE_DELAY, _apagar_rele)
    _rele_timer.start()
    return True

def desactivar_rele():
    try:
        requests.get(f"http://{ESP2_IP}:{ESP2_PORT}/rele/off", timeout=5)
        return True
    except Exception as e:
        print(f"[RELÉ] Error: {e}")
        return False


# ── Endpoints ─────────────────────────────────────────────────────────────────

@app.route("/validar", methods=["POST"])
def validar():
    imagen_bytes = request.get_data()
    if not imagen_bytes:
        return jsonify({"error": "sin imagen"}), 400

    # Guardar captura
    ts    = datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
    ruta  = capturas_dir / f"{ts}.jpg"
    ruta.write_bytes(imagen_bytes)

    # Detectar patente
    patente = leer_patente(imagen_bytes)
    modo_demo = patente is None
    if modo_demo:
        # Sin OCR: busca si hay header de hint enviado por el simulador
        patente = request.headers.get("X-Filename", "DEMO000")
        patente = "".join(c for c in patente.upper() if c.isalnum())[:8] or "DEMO000"

    valida = patente in patentes_autorizadas

    print(f"[VALIDAR] {ruta.name} | patente={patente} | válida={valida} | OCR={'no (demo)' if modo_demo else 'sí'}")

    if valida:
        activar_rele_con_timer()
    else:
        desactivar_rele()

    return jsonify({
        "patente":  patente,
        "valida":   valida,
        "demo_mode": modo_demo,
        "captura":  ruta.name,
    })


@app.route("/patentes", methods=["GET"])
def listar_patentes():
    return jsonify(sorted(patentes_autorizadas))


@app.route("/patentes", methods=["POST"])
def agregar_patente():
    body = request.get_json(silent=True) or {}
    p = body.get("patente", "").upper().strip()
    if not p:
        return jsonify({"error": "falta campo 'patente'"}), 400
    patentes_autorizadas.add(p)
    print(f"[PATENTES] Agregada: {p}")
    return jsonify({"ok": True, "patente": p})


@app.route("/patentes/<patente>", methods=["DELETE"])
def eliminar_patente(patente: str):
    p = patente.upper()
    patentes_autorizadas.discard(p)
    return jsonify({"ok": True})


@app.route("/capturas", methods=["GET"])
def listar_capturas():
    archivos = sorted(capturas_dir.glob("*.jpg"), reverse=True)[:20]
    return jsonify([f.name for f in archivos])


@app.route("/ping", methods=["GET"])
def ping():
    return jsonify({"ok": True, "esp2": f"{ESP2_IP}:{ESP2_PORT}"})


if __name__ == "__main__":
    print("=" * 50)
    print("  Servicio de validación de patentes")
    print(f"  ESP2 relé → {ESP2_IP}:{ESP2_PORT}")
    print(f"  Relé activo por {RELE_DELAY}s al validar")
    print(f"  Capturas → {capturas_dir}")
    print(f"  Patentes autorizadas: {sorted(patentes_autorizadas)}")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=False)
