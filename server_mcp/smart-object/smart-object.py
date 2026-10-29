#!/usr/bin/env python3
"""Puente entre Claude (MCP por stdio) y el ESP32 (MQTT).

Configuración → mensajes MQTT → estado protegido → tres herramientas MCP.
Los logs van a stderr; stdout se reserva para el protocolo de Claude.
"""

import argparse
import json
import logging
import math
import os
import re
import sys
import threading
import time
import uuid
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Annotated, Any

import paho.mqtt.client as mqtt
from mcp.server.fastmcp import FastMCP
from pydantic import Field

LOG = logging.getLogger("smart-object")
CONFIG_ENV_VAR = "SMART_OBJECT_CONFIG"
DEFAULT_CONFIG_PATH = Path(__file__).resolve().with_name("smart-object.config.json")
SERVO_MIN_ANGLE, SERVO_MAX_ANGLE = 0, 180


class ConfigError(Exception):
    """El archivo de configuración falta o contiene valores incorrectos."""


def load_config(path: Path) -> dict:
    """Lee el mismo JSON y comprueba tipos, rangos y tópicos."""
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
        # Campos obligatorios y tipos esperados en cada sección del JSON.
        expected = {
            "mqtt": {"host": str, "port": int, "client_id_prefix": str,
                     "keepalive": int, "reconnect_min_delay": (int, float),
                     "reconnect_max_delay": (int, float)},
            "topics": dict.fromkeys(("distance", "servo_command", "servo_state", "availability"), str),
            "distance": dict.fromkeys(("stale_after_seconds", "min_cm", "max_cm"), (int, float)),
            "logging": {"level": str},
        }
        for section, fields in expected.items():
            if not isinstance(config, dict) or not isinstance(config.get(section), dict):
                raise ValueError(f"Sección inválida: {section}")
            for key, kind in fields.items():
                value = config[section].get(key)
                # Python considera bool un tipo de int; aquí rechazamos true/false como números.
                if isinstance(value, bool) or not isinstance(value, kind):
                    raise ValueError(f"Valor inválido: {section}.{key}")
        mq, distance = config["mqtt"], config["distance"]
        mq["host"] = mq["host"].strip()
        config["logging"]["level"] = config["logging"]["level"].upper()
        # Un tipo correcto no basta: también comprobamos los límites de cada valor.
        checks = {
            "mqtt.host": bool(mq["host"]),
            "mqtt.port": 1 <= mq["port"] <= 65535,
            "mqtt.keepalive": mq["keepalive"] >= 1,
            "mqtt.reconnect_delays": 0 < mq["reconnect_min_delay"] <= mq["reconnect_max_delay"] < math.inf,
            "distance.stale_after_seconds": 0 < distance["stale_after_seconds"] < math.inf,
            "distance.range": 0 <= distance["min_cm"] < distance["max_cm"] < math.inf,
            "logging.level": config["logging"]["level"] in ("DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"),
        }
        for name, valid in checks.items():
            if not valid:
                raise ValueError(f"Valor fuera de rango: {name}")
        # Evita mezclar comandos y lecturas o suscribirse a múltiples tópicos por accidente.
        topics = list(config["topics"][key] for key in expected["topics"])
        if len(set(topics)) != 4 or any(not topic or "+" in topic or "#" in topic for topic in topics):
            raise ValueError("Los cuatro tópicos deben ser distintos, no vacíos y sin + ni #")
        return config
    except (OSError, ValueError) as exc:
        raise ConfigError(f"{path}: {exc}") from exc


def _decode(payload: bytes) -> str | None:
    try:
        # MQTT entrega bytes: los convertimos a texto y descartamos mensajes vacíos.
        return payload.decode("utf-8").strip() or None
    except UnicodeDecodeError:
        return None


def parse_distance(payload: bytes, min_cm: float, max_cm: float) -> float | None:
    text = _decode(payload)
    # Acepta números decimales como 23.45; rechaza texto y notación como 1e2.
    if text is None or not re.fullmatch(r"[+-]?\d+(?:\.\d+)?", text):
        return None
    value = float(text)
    return value if math.isfinite(value) and min_cm <= value <= max_cm else None


def parse_angle(payload: bytes) -> int | None:
    text = _decode(payload)
    # El estado del servo debe ser un entero escrito en texto, dentro de 0 a 180.
    if text is None or not re.fullmatch(r"\d{1,3}", text):
        return None
    value = int(text)
    return value if 0 <= value <= 180 else None


def parse_availability(payload: bytes) -> str | None:
    text = _decode(payload)
    return text if text in ("online", "offline") else None


class SmartObject:
    """Administra MQTT y los últimos datos recibidos, sin esperar al ESP32."""

    def __init__(self, config: dict):
        self.config = config
        # MQTT recibe en otro hilo; el lock impide leer datos mientras se actualizan.
        self.lock = threading.Lock()
        self.state = dict(connected=False, distance=None, servo=None,
                          availability=None, availability_utc=None)
        mq, topics, distance = config["mqtt"], config["topics"], config["distance"]
        # Cada tópico se asocia con el nombre del dato y su función de validación.
        self.routes = {
            topics["distance"]: ("distance", lambda p: parse_distance(p, distance["min_cm"], distance["max_cm"])),
            topics["servo_state"]: ("servo", parse_angle),
            topics["availability"]: ("availability", parse_availability),
        }
        # El UUID evita que dos instancias usen el mismo identificador en el broker.
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                  client_id=f"{mq['client_id_prefix']}{uuid.uuid4()}")
        self.client.enable_logger(LOG)
        self.client.reconnect_delay_set(math.ceil(mq["reconnect_min_delay"]), math.ceil(mq["reconnect_max_delay"]))
        # Paho llama estas funciones cuando cambia la conexión o llega un mensaje.
        self.client.on_connect = self.on_connect
        self.client.on_connect_fail = self.on_connect_fail
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message

    def snapshot(self) -> dict:
        # Las lecturas se reemplazan completas; esta copia puede usarse sin el lock.
        with self.lock:
            return {**self.state, "now_mono": time.monotonic()}

    def set_connected(self, connected: bool):
        with self.lock:
            self.state["connected"] = connected

    def start(self):
        mq = self.config["mqtt"]
        try:
            # La conexión y la recepción se ejecutan en segundo plano para no bloquear MCP.
            self.client.connect_async(mq["host"], mq["port"], keepalive=mq["keepalive"])
            self.client.loop_start()
        except Exception:
            LOG.exception("No se pudo iniciar MQTT; MCP sigue disponible")

    def stop(self):
        for action in (self.client.disconnect, self.client.loop_stop):
            try:
                action()
            except Exception:
                LOG.debug("Error al cerrar MQTT", exc_info=True)
        self.set_connected(False)

    def on_connect(self, client, userdata, flags, reason_code, properties):
        self.set_connected(not reason_code.is_failure)
        if reason_code.is_failure:
            LOG.error("Conexión MQTT rechazada: %s", reason_code)
            return
        # Renovamos las suscripciones también después de una reconexión.
        for topic in self.routes:
            result, _ = client.subscribe(topic, qos=0)
            if result != mqtt.MQTT_ERR_SUCCESS:
                LOG.error("Error al suscribirse a %s: %s", topic, mqtt.error_string(result))

    def on_connect_fail(self, client, userdata):
        self.set_connected(False)
        LOG.warning("Falló la conexión MQTT; se reintentará automáticamente")

    def on_disconnect(self, client, userdata, flags, reason_code, properties):
        self.set_connected(False)
        LOG.warning("MQTT desconectado: %s", reason_code)

    def on_message(self, client, userdata, msg):
        # Los callbacks solo validan y guardan; nunca publican comandos.
        try:
            if msg.topic not in self.routes:
                return
            name, parse = self.routes[msg.topic]
            value = parse(bytes(msg.payload))
            if value is None:
                LOG.warning("Mensaje inválido en %s: %r", msg.topic, msg.payload[:40])
                return
            now = datetime.now(timezone.utc)
            with self.lock:
                if name == "availability":
                    self.state.update(availability=value, availability_utc=now)
                else:
                    # UTC indica cuándo llegó el mensaje; monotonic permite calcular su edad
                    # sin depender de ajustes del reloj. retain indica si venía guardado en el broker.
                    self.state[name] = dict(value=value, received_utc=now,
                                            received_mono=time.monotonic(), retained=bool(msg.retain))
        except Exception:
            LOG.exception("Error al procesar %s", msg.topic)

    def publish_servo_command(self, angle: int) -> tuple[bool, str]:
        if not self.snapshot()["connected"]:
            return False, "MQTT is not connected to the broker; command was not sent."
        try:
            # Se envía texto (por ejemplo, "90"). retain=False evita guardar un comando
            # que pudiera repetirse al reconectar; QoS 0 no confirma la recepción del ESP32.
            info = self.client.publish(self.config["topics"]["servo_command"], str(angle), qos=0, retain=False)
            if info.rc != mqtt.MQTT_ERR_SUCCESS:
                return False, f"Publish failed: {mqtt.error_string(info.rc)}"
            return True, "Command handed to the MQTT client for delivery."
        except Exception as exc:
            LOG.exception("Error al publicar el ángulo")
            return False, f"Publish failed: {exc}"


# Estas funciones mantienen el formato de respuesta que recibe Claude.
def _iso(dt: datetime | None) -> str | None:
    return dt.isoformat(timespec="milliseconds") if dt else None


def _connection_info(snap: dict) -> dict[str, Any]:
    """Al desconectarse MQTT, la disponibilidad guardada deja de ser fiable."""
    if snap["connected"]:
        availability = snap["availability"] or "unknown"
        note = None
    else:
        availability = "unknown"
        note = ("MQTT is disconnected, so the cached device availability cannot be trusted."
                if snap["availability"] else None)
    info: dict[str, Any] = {
        "mqtt_connected": snap["connected"],
        "device_availability": availability,
        "last_reported_availability": snap["availability"],
        "last_reported_availability_received_utc": _iso(snap["availability_utc"]),
    }
    if note:
        info["availability_note"] = note
    return info


def _reading_info(reading: dict, now_mono: float) -> dict[str, Any]:
    return {
        "received_utc": _iso(reading["received_utc"]),
        "received_utc_meaning": "Time this server RECEIVED the MQTT message, not when it was measured/reported by the ESP32.",
        "age_seconds": round(max(0.0, now_mono - reading["received_mono"]), 1),
        "retained": reading["retained"],
    }


# Herramientas que Claude puede llamar.
def build_server(config: dict) -> FastMCP:
    # Las tres herramientas comparten la misma conexión y las últimas lecturas recibidas.
    service = SmartObject(config)

    # MCP inicia MQTT al abrir la sesión y lo cierra aunque ocurra un error.
    @asynccontextmanager
    async def lifespan(_server: FastMCP):
        service.start()
        try:
            yield
        finally:
            service.stop()

    mcp = FastMCP("smart-object", lifespan=lifespan,
                  instructions="Read ESP32 distance, command its servo (0-180) and read the last "
                               "servo angle it reported, over MQTT.")

    @mcp.tool()
    def get_distance() -> dict[str, Any]:
        """Get the latest valid distance (cm) from the ESP32 HC-SR04 sensor, with freshness metadata."""
        snap = service.snapshot()
        result: dict[str, Any] = _connection_info(snap)
        if snap["distance"] is None:
            result["available"] = False
            result["message"] = ("No valid distance measurement has been received yet. The ESP32 publishes "
                                 "about every 5 seconds when MQTT is connected and the reading is valid; try again shortly.")
            return result
        reading = snap["distance"]
        info = _reading_info(reading, snap["now_mono"])
        # Una lectura disponible puede ser antigua: informamos ambas cosas por separado.
        stale = info["age_seconds"] > config["distance"]["stale_after_seconds"]
        result.update(info)
        result.update({
            "available": True,
            "distance_cm": reading["value"],
            "stale": stale,
            "stale_after_seconds": config["distance"]["stale_after_seconds"],
        })
        notes = []
        # Un mensaje retenido puede ser antiguo aunque acabemos de recibirlo.
        if reading["retained"]:
            notes.append("This value arrived as a RETAINED message: it is the broker's stored last value, "
                         "its original measurement time is unknown, and it may be old even though it was just received.")
        if stale:
            notes.append(f"No newer valid measurement for more than {config['distance']['stale_after_seconds']:g}s; "
                         "the value may no longer reflect the current distance.")
        if not snap["connected"]:
            notes.append("MQTT is currently disconnected; this is the last cached value.")
        result["message"] = " ".join(notes) or "Recent distance received from the ESP32."
        return result

    @mcp.tool()
    def set_servo_angle(angle: Annotated[int, Field(strict=True)]) -> dict[str, Any]:
        """Command the ESP32 servo to an integer angle from 0 to 180 degrees (publishes over MQTT; does not confirm movement)."""
        # Validamos antes de publicar para impedir ángulos inválidos desde las herramientas.
        if isinstance(angle, bool) or not isinstance(angle, int):
            return {"success": False, "error": f"angle must be an integer from {SERVO_MIN_ANGLE} to "
                                               f"{SERVO_MAX_ANGLE}, got {angle!r}."}
        if not SERVO_MIN_ANGLE <= angle <= SERVO_MAX_ANGLE:
            return {"success": False, "error": f"angle {angle} is out of range; allowed values are "
                                               f"{SERVO_MIN_ANGLE} to {SERVO_MAX_ANGLE}."}
        accepted, detail = service.publish_servo_command(angle)
        if not accepted:
            return {"success": False, "requested_angle": angle, "error": detail}
        return {
            "success": True,
            "requested_angle": angle,
            "published_to": config["topics"]["servo_command"],
            "message": ("Command published (QoS 0, not retained). This only means the local MQTT client accepted it. "
                        "It is NOT an acknowledgment from the ESP32 and does not prove the servo moved. "
                        "Call get_servo_status afterwards to see the angle the firmware reports."),
        }

    @mcp.tool()
    def get_servo_status() -> dict[str, Any]:
        """Get the last servo angle reported by the ESP32 firmware (not a physically measured position)."""
        snap = service.snapshot()
        result: dict[str, Any] = _connection_info(snap)
        if snap["servo"] is None:
            result["available"] = False
            result["message"] = ("No valid servo state has been received yet. The firmware reports its angle only "
                                 "after accepting a command and after each MQTT reconnection, so a long silence is normal.")
            return result
        reading = snap["servo"]
        result.update(_reading_info(reading, snap["now_mono"]))
        # Es el ángulo que informó el firmware, no una medición física de la posición.
        result.update({"available": True, "last_reported_angle": reading["value"]})
        notes = ["This is the last angle REPORTED by the firmware (the angle it commanded), not a measured physical position.",
                 "Age alone does not indicate a fault: the firmware only reports after commands and reconnections.",
                 "The protocol has no command IDs, so this state does not confirm that any particular command was executed."]
        # Un mensaje retenido puede ser antiguo aunque acabemos de recibirlo.
        if reading["retained"]:
            notes.append("It arrived as a RETAINED message and may be historical.")
        if not snap["connected"]:
            notes.append("MQTT is currently disconnected; this is the last cached value.")
        result["message"] = " ".join(notes)
        return result

    return mcp


def _resolve_config_path(argv: list[str]) -> Path:
    parser = argparse.ArgumentParser(description="Smart object MCP server (stdio)")
    parser.add_argument("--config", help=f"path to the JSON config (or set {CONFIG_ENV_VAR}); "
                                         f"default: {DEFAULT_CONFIG_PATH}")
    args, _ = parser.parse_known_args(argv)
    # Prioridad: --config, variable de entorno y, por último, JSON junto al script.
    chosen = args.config or os.environ.get(CONFIG_ENV_VAR)
    return Path(chosen).expanduser().resolve() if chosen else DEFAULT_CONFIG_PATH


def main(argv: list[str] | None = None) -> int:
    # Nunca usar print para depurar: stdout transporta los mensajes MCP de Claude.
    logging.basicConfig(stream=sys.stderr, level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    try:
        config = load_config(_resolve_config_path(sys.argv[1:] if argv is None else argv))
    except ConfigError as exc:
        LOG.error("Configuration error: %s", exc)
        return 2
    logging.getLogger().setLevel(config["logging"]["level"])
    # Claude ejecuta este script y se comunica con él por entrada/salida estándar.
    build_server(config).run(transport="stdio")
    return 0


if __name__ == "__main__":
    sys.exit(main())
