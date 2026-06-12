#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Proxy de validación de patentes.
Recibe imagen del ESP (HTTP), llama a Plate Recognizer (HTTPS) y devuelve resultado.

Uso:
    python servicio.py
"""

from flask import Flask, request, jsonify
import requests
from pathlib import Path
from datetime import datetime

app = Flask(__name__)

API_TOKEN = "7309a6b94012c5101a316a65445471678c814b66"
API_URL   = "https://api.platerecognizer.com/v1/plate-reader/"

capturas_dir = Path(__file__).parent / "capturas"
capturas_dir.mkdir(exist_ok=True)


def llamar_plate_recognizer(imagen_bytes: bytes) -> dict:
    resp = requests.post(
        API_URL,
        headers={"Authorization": f"Token {API_TOKEN}"},
        files={"upload": ("plate.jpg", imagen_bytes, "image/jpeg")},
        timeout=15,
    )
    resp.raise_for_status()
    return resp.json()


@app.route("/validar", methods=["POST"])
def validar():
    imagen_bytes = request.get_data()
    if not imagen_bytes:
        return jsonify({"patente": "", "debug": "Sin imagen recibida"}), 400

    # Guardar captura
    ts   = datetime.now().strftime("%Y%m%d_%H%M%S")
    ruta = capturas_dir / f"{ts}.jpg"
    ruta.write_bytes(imagen_bytes)
    print(f"[CAM] Imagen recibida: {len(imagen_bytes)} bytes → {ruta.name}")

    try:
        data    = llamar_plate_recognizer(imagen_bytes)
        results = data.get("results", [])

        if not results:
            print("[API] Sin resultados")
            return jsonify({"patente": "", "debug": "Patente no detectada en la imagen"})

        patente = results[0]["plate"].upper()
        score   = results[0]["score"]
        print(f"[API] Detectada: {patente} (score {score:.2f})")
        return jsonify({"patente": patente, "score": score, "debug": f"Detectada con score {score:.2f}"})

    except requests.HTTPError as e:
        print(f"[API] HTTP error: {e.response.status_code}")
        return jsonify({"patente": "", "debug": f"Error API HTTP {e.response.status_code}"}), 502
    except Exception as e:
        print(f"[API] Error: {e}")
        return jsonify({"patente": "", "debug": f"Error: {str(e)}"}), 502


@app.route("/ping")
def ping():
    return jsonify({"ok": True})


if __name__ == "__main__":
    print("=" * 45)
    print("  Proxy de validación de patentes")
    print(f"  Escuchando en http://0.0.0.0:5000")
    print(f"  Capturas → {capturas_dir}")
    print("=" * 45)
    app.run(host="0.0.0.0", port=5000, debug=False)
