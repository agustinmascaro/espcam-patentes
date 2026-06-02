#!/usr/bin/env python3
"""
Simulador de ESP-CAM
Envía imágenes desde disco hacia el ESP1 como si fuera una cámara real.

Uso:
    python simulador.py [IP_ESP1] [PUERTO]
    python simulador.py 192.168.1.100
    python simulador.py 192.168.1.100 80
"""

import os
import sys
import time
import json
from pathlib import Path

try:
    import requests
except ImportError:
    print("[ERROR] Falta la librería 'requests'. Ejecuta: pip install requests")
    sys.exit(1)

try:
    from PIL import Image
    import io
    RESIZE_DISPONIBLE = True
except ImportError:
    RESIZE_DISPONIBLE = False

# ── Configuración por defecto ────────────────────────────────────────────────
ESP1_IP      = "192.168.1.100"
ESP1_PORT    = 80
IMAGENES_DIR = Path(__file__).parent / "imagenes"
TIMEOUT      = 10   # segundos por request
# Tamaño al que se reduce la imagen antes de enviar (simula resolución ESP-CAM)
# None = no redimensionar
RESIZE       = (640, 480)

EXTENSIONES = {".jpg", ".jpeg", ".png", ".bmp"}


def limpiar():
    os.system("clear" if os.name != "nt" else "cls")


def listar_imagenes() -> list[Path]:
    if not IMAGENES_DIR.exists():
        IMAGENES_DIR.mkdir(parents=True)
    return sorted(f for f in IMAGENES_DIR.iterdir() if f.suffix.lower() in EXTENSIONES)


def preparar_imagen(ruta: Path) -> bytes:
    """Lee la imagen y opcionalmente la redimensiona para simular ESP-CAM."""
    if RESIZE_DISPONIBLE and RESIZE:
        img = Image.open(ruta)
        img = img.convert("RGB")
        img.thumbnail(RESIZE, Image.LANCZOS)
        buf = io.BytesIO()
        img.save(buf, format="JPEG", quality=85)
        return buf.getvalue()
    else:
        return ruta.read_bytes()


def enviar_imagen(ruta: Path, esp_ip: str, esp_port: int) -> dict:
    url = f"http://{esp_ip}:{esp_port}/imagen"
    datos = preparar_imagen(ruta)
    resp = requests.post(
        url,
        data=datos,
        headers={"Content-Type": "image/jpeg", "X-Filename": ruta.name},
        timeout=TIMEOUT,
    )
    resultado = {"status": resp.status_code, "body": resp.text, "bytes_enviados": len(datos)}
    try:
        resultado["json"] = resp.json()
    except Exception:
        pass
    return resultado


def formatear_respuesta(r: dict) -> str:
    if "json" in r:
        j = r["json"]
        patente = j.get("patente", "?")
        valida  = j.get("valida", "?")
        rele    = "ABIERTO" if valida else "CERRADO"
        return (
            f"  HTTP {r['status']} | {r['bytes_enviados']} bytes enviados\n"
            f"  Patente: {patente} | Válida: {valida} | Relé: {rele}"
        )
    return f"  HTTP {r['status']} | {r['bytes_enviados']} bytes | Respuesta: {r['body'][:120]}"


def verificar_conexion(esp_ip: str, esp_port: int) -> bool:
    try:
        r = requests.get(f"http://{esp_ip}:{esp_port}/ping", timeout=3)
        return r.status_code == 200
    except Exception:
        return False


def menu_principal(esp_ip: str, esp_port: int):
    imagenes = listar_imagenes()

    while True:
        limpiar()
        print("=" * 54)
        print("  SIMULADOR ESP-CAM")
        conectado = verificar_conexion(esp_ip, esp_port)
        estado = "[OK]" if conectado else "[SIN CONEXION]"
        print(f"  ESP1 → {esp_ip}:{esp_port}  {estado}")
        if RESIZE_DISPONIBLE and RESIZE:
            print(f"  Resolución simulada: {RESIZE[0]}x{RESIZE[1]} JPEG")
        else:
            print("  Resolución: original (instala Pillow para resize)")
        print("=" * 54)

        if not imagenes:
            print(f"\n  [!] No hay imágenes en:\n      {IMAGENES_DIR}")
            print("      Coloca archivos .jpg/.png y presiona [r].\n")
        else:
            print(f"\n  Imágenes ({len(imagenes)}):\n")
            for i, img in enumerate(imagenes, 1):
                size_kb = img.stat().st_size // 1024
                print(f"    [{i:2d}] {img.name:<35} {size_kb:>5} KB")

        print()
        print("  [t] Enviar TODAS en secuencia")
        print("  [c] Cambiar IP del ESP1")
        print("  [r] Recargar lista de imágenes")
        print("  [s] Salir")
        print()
        opcion = input("  Opción (número o letra): ").strip().lower()

        if opcion == "s":
            print("\n  Hasta luego.")
            break

        elif opcion == "r":
            imagenes = listar_imagenes()

        elif opcion == "c":
            nuevo = input(f"  Nueva IP [{esp_ip}]: ").strip()
            if nuevo:
                esp_ip = nuevo
            nuevo_p = input(f"  Nuevo puerto [{esp_port}]: ").strip()
            if nuevo_p.isdigit():
                esp_port = int(nuevo_p)

        elif opcion == "t" and imagenes:
            print()
            for img in imagenes:
                print(f"  → {img.name}")
                try:
                    r = enviar_imagen(img, esp_ip, esp_port)
                    print(formatear_respuesta(r))
                except requests.exceptions.ConnectionError:
                    print(f"  [ERROR] No se pudo conectar a {esp_ip}:{esp_port}")
                except requests.exceptions.Timeout:
                    print(f"  [TIMEOUT] Sin respuesta en {TIMEOUT}s")
                except Exception as e:
                    print(f"  [ERROR] {e}")
                print()
                time.sleep(1.5)
            input("  [ENTER para volver al menú]")

        elif opcion.isdigit():
            idx = int(opcion) - 1
            if 0 <= idx < len(imagenes):
                img = imagenes[idx]
                print(f"\n  Enviando: {img.name} ...")
                try:
                    r = enviar_imagen(img, esp_ip, esp_port)
                    print(formatear_respuesta(r))
                except requests.exceptions.ConnectionError:
                    print(f"  [ERROR] No se pudo conectar a {esp_ip}:{esp_port}")
                except requests.exceptions.Timeout:
                    print(f"  [TIMEOUT] Sin respuesta en {TIMEOUT}s")
                except Exception as e:
                    print(f"  [ERROR] {e}")
                input("\n  [ENTER para continuar]")
            else:
                print("  Número inválido.")
                time.sleep(1)


def main():
    esp_ip   = ESP1_IP
    esp_port = ESP1_PORT

    if len(sys.argv) > 1:
        esp_ip = sys.argv[1]
    if len(sys.argv) > 2:
        esp_port = int(sys.argv[2])

    try:
        menu_principal(esp_ip, esp_port)
    except KeyboardInterrupt:
        print("\n\n  Interrumpido.")


if __name__ == "__main__":
    main()
