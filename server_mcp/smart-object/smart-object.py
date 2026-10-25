#!/usr/bin/env python3
"""MCP server (stdio) bridging an AI assistant to the ESP32 smart object over MQTT.

Tools:
    get_distance      - latest valid HC-SR04 distance received over MQTT
    set_servo_angle   - publish a servo command (0-180) to the firmware
    get_servo_status  - last servo angle reported by the firmware

Design rules:
    * stdout is reserved for the MCP protocol; all logs go to stderr.
    * MQTT callbacks only validate and store state; they never publish.
    * Shared state is guarded by a lock that is never held during network I/O.
    * Tools never wait for future MQTT messages.

Configuration: smart-object.config.json next to this file, or an explicit path
via ``--config PATH`` or the SMART_OBJECT_CONFIG environment variable.
"""

from __future__ import annotations

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
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Annotated, Any, AsyncIterator

import paho.mqtt.client as mqtt
from mcp.server.fastmcp import FastMCP
from pydantic import Field

LOG = logging.getLogger("smart-object")

CONFIG_ENV_VAR = "SMART_OBJECT_CONFIG"
DEFAULT_CONFIG_PATH = Path(__file__).resolve().parent / "smart-object.config.json"

SERVO_MIN_ANGLE = 0
SERVO_MAX_ANGLE = 180

_DECIMAL_RE = re.compile(r"[+-]?\d+(?:\.\d+)?")
_INTEGER_RE = re.compile(r"\d{1,3}")


# --------------------------------------------------------------------------- #
# Configuration
# --------------------------------------------------------------------------- #
class ConfigError(Exception):
    """Raised when the configuration file is missing or invalid."""


@dataclass(frozen=True)
class Config:
    host: str
    port: int
    client_id_prefix: str
    keepalive: int
    reconnect_min_delay: float
    reconnect_max_delay: float
    topic_distance: str
    topic_servo_command: str
    topic_servo_state: str
    topic_availability: str
    stale_after_seconds: float
    distance_min_cm: float
    distance_max_cm: float
    log_level: str


def _section(data: dict, name: str, path: Path) -> dict:
    value = data.get(name)
    if not isinstance(value, dict):
        raise ConfigError(f"{path}: missing or invalid section '{name}' (expected a JSON object)")
    return value


def _field(section: dict, section_name: str, key: str, kinds: tuple, path: Path) -> Any:
    if key not in section:
        raise ConfigError(f"{path}: missing '{section_name}.{key}'")
    value = section[key]
    if isinstance(value, bool) or not isinstance(value, kinds):
        names = "/".join(k.__name__ for k in kinds)
        raise ConfigError(f"{path}: '{section_name}.{key}' must be of type {names}, got {value!r}")
    return value


def _topic(section: dict, key: str, path: Path) -> str:
    value = _field(section, "topics", key, (str,), path)
    if not value or any(c in value for c in "+#"):
        raise ConfigError(f"{path}: 'topics.{key}' must be a non-empty topic without wildcards (+ or #), got {value!r}")
    return value


def load_config(path: Path) -> Config:
    """Read and validate the JSON configuration file."""
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ConfigError(
            f"Cannot read configuration file '{path}': {exc}. "
            f"Pass --config PATH or set {CONFIG_ENV_VAR}."
        ) from exc
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ConfigError(f"{path}: invalid JSON at line {exc.lineno}, column {exc.colno}: {exc.msg}") from exc
    if not isinstance(data, dict):
        raise ConfigError(f"{path}: top-level value must be a JSON object")

    mq = _section(data, "mqtt", path)
    tp = _section(data, "topics", path)
    ds = _section(data, "distance", path)
    lg = _section(data, "logging", path)

    host = _field(mq, "mqtt", "host", (str,), path).strip()
    port = _field(mq, "mqtt", "port", (int,), path)
    prefix = _field(mq, "mqtt", "client_id_prefix", (str,), path)
    keepalive = _field(mq, "mqtt", "keepalive", (int,), path)
    rmin = _field(mq, "mqtt", "reconnect_min_delay", (int, float), path)
    rmax = _field(mq, "mqtt", "reconnect_max_delay", (int, float), path)
    stale = _field(ds, "distance", "stale_after_seconds", (int, float), path)
    dmin = _field(ds, "distance", "min_cm", (int, float), path)
    dmax = _field(ds, "distance", "max_cm", (int, float), path)
    level = _field(lg, "logging", "level", (str,), path).upper()

    if not host:
        raise ConfigError(f"{path}: 'mqtt.host' must not be empty")
    if not 1 <= port <= 65535:
        raise ConfigError(f"{path}: 'mqtt.port' must be between 1 and 65535, got {port}")
    if keepalive < 1:
        raise ConfigError(f"{path}: 'mqtt.keepalive' must be >= 1 second, got {keepalive}")
    if not 0 < rmin <= rmax:
        raise ConfigError(
            f"{path}: need 0 < mqtt.reconnect_min_delay <= mqtt.reconnect_max_delay, got {rmin} and {rmax}"
        )
    if stale <= 0:
        raise ConfigError(f"{path}: 'distance.stale_after_seconds' must be > 0, got {stale}")
    if not (math.isfinite(dmin) and math.isfinite(dmax) and 0 <= dmin < dmax):
        raise ConfigError(f"{path}: need 0 <= distance.min_cm < distance.max_cm, got {dmin} and {dmax}")
    if level not in ("DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"):
        raise ConfigError(f"{path}: 'logging.level' must be DEBUG, INFO, WARNING, ERROR or CRITICAL, got {level!r}")

    topics = {k: _topic(tp, k, path) for k in ("distance", "servo_command", "servo_state", "availability")}
    if len(set(topics.values())) != len(topics):
        raise ConfigError(f"{path}: the four topics in 'topics' must all be different")

    return Config(
        host=host, port=port, client_id_prefix=prefix, keepalive=keepalive,
        reconnect_min_delay=float(rmin), reconnect_max_delay=float(rmax),
        topic_distance=topics["distance"], topic_servo_command=topics["servo_command"],
        topic_servo_state=topics["servo_state"], topic_availability=topics["availability"],
        stale_after_seconds=float(stale), distance_min_cm=float(dmin), distance_max_cm=float(dmax),
        log_level=level,
    )


# --------------------------------------------------------------------------- #
# Payload validation (pure functions)
# --------------------------------------------------------------------------- #
def _decode(payload: bytes) -> str | None:
    """Strict UTF-8 decode; None for invalid UTF-8 or empty/blank payloads."""
    try:
        text = payload.decode("utf-8").strip()
    except UnicodeDecodeError:
        return None
    return text or None


def parse_distance(payload: bytes, min_cm: float, max_cm: float) -> float | None:
    """Return the distance in cm, or None if the payload is not a valid reading."""
    text = _decode(payload)
    if text is None or not _DECIMAL_RE.fullmatch(text):
        return None
    value = float(text)
    if not math.isfinite(value) or not min_cm <= value <= max_cm:
        return None
    return value


def parse_angle(payload: bytes) -> int | None:
    """Return the servo angle (0-180) from a plain-text integer payload, else None."""
    text = _decode(payload)
    if text is None or not _INTEGER_RE.fullmatch(text):
        return None
    value = int(text)
    return value if SERVO_MIN_ANGLE <= value <= SERVO_MAX_ANGLE else None


def parse_availability(payload: bytes) -> str | None:
    text = _decode(payload)
    return text if text in ("online", "offline") else None


# --------------------------------------------------------------------------- #
# Shared state
# --------------------------------------------------------------------------- #
@dataclass(frozen=True)
class Reading:
    """A validated value together with how/when this server received it."""
    value: Any
    received_utc: datetime
    received_mono: float
    retained: bool


@dataclass(frozen=True)
class Snapshot:
    """Consistent copy of the whole state, taken under the lock."""
    connected: bool
    distance: Reading | None
    servo: Reading | None
    availability: str | None
    availability_utc: datetime | None
    now_mono: float


class State:
    """Thread-safe store. Only short critical sections; no I/O under the lock."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._connected = False
        self._distance: Reading | None = None
        self._servo: Reading | None = None
        self._availability: str | None = None
        self._availability_utc: datetime | None = None

    def set_connected(self, connected: bool) -> None:
        with self._lock:
            self._connected = connected

    def store_distance(self, value: float, retained: bool) -> None:
        reading = Reading(value, datetime.now(timezone.utc), time.monotonic(), retained)
        with self._lock:
            self._distance = reading

    def store_servo(self, value: int, retained: bool) -> None:
        reading = Reading(value, datetime.now(timezone.utc), time.monotonic(), retained)
        with self._lock:
            self._servo = reading

    def store_availability(self, value: str) -> None:
        now = datetime.now(timezone.utc)
        with self._lock:
            self._availability = value
            self._availability_utc = now

    def snapshot(self) -> Snapshot:
        with self._lock:
            return Snapshot(self._connected, self._distance, self._servo,
                            self._availability, self._availability_utc, time.monotonic())


# --------------------------------------------------------------------------- #
# MQTT service
# --------------------------------------------------------------------------- #
class MqttService:
    """Owns the paho client: background loop, auto-reconnect, subscriptions, publishing."""

    def __init__(self, config: Config, state: State) -> None:
        self._cfg = config
        self._state = state
        self.client_id = f"{config.client_id_prefix}{uuid.uuid4()}"
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=self.client_id)
        self._client.enable_logger(LOG.getChild("paho"))
        self._client.reconnect_delay_set(
            min_delay=max(1, math.ceil(config.reconnect_min_delay)),
            max_delay=max(1, math.ceil(config.reconnect_max_delay)),
        )
        self._client.on_connect = self._on_connect
        self._client.on_connect_fail = self._on_connect_fail
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message
        self._topic_handlers = {
            config.topic_distance: self._handle_distance,
            config.topic_servo_state: self._handle_servo_state,
            config.topic_availability: self._handle_availability,
        }

    # -- lifecycle ---------------------------------------------------------
    def start(self) -> None:
        """Begin connecting in the background; never raises on network failure."""
        LOG.info("Starting MQTT client %s -> %s:%d", self.client_id, self._cfg.host, self._cfg.port)
        try:
            self._client.connect_async(self._cfg.host, self._cfg.port, keepalive=self._cfg.keepalive)
            self._client.loop_start()
        except Exception:  # noqa: BLE001 - the MCP server must stay up regardless
            LOG.exception("Could not start MQTT client; MCP server continues without MQTT")

    def stop(self) -> None:
        LOG.info("Stopping MQTT client")
        try:
            self._client.disconnect()
        except Exception:  # noqa: BLE001
            LOG.debug("disconnect failed", exc_info=True)
        try:
            self._client.loop_stop()
        except Exception:  # noqa: BLE001
            LOG.debug("loop_stop failed", exc_info=True)
        self._state.set_connected(False)

    # -- callbacks (validate and store only; never publish) ------------------
    def _on_connect(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code.is_failure:
            LOG.error("MQTT broker refused connection: %s", reason_code)
            return
        self._state.set_connected(True)
        LOG.info("MQTT connected to %s:%d (session_present=%s)", self._cfg.host, self._cfg.port,
                 getattr(flags, "session_present", None))
        # Resubscribe on every connection; QoS 0 matches the firmware.
        for topic in self._topic_handlers:
            result, _ = client.subscribe(topic, qos=0)
            if result != mqtt.MQTT_ERR_SUCCESS:
                LOG.error("Subscribe to %s failed: %s", topic, mqtt.error_string(result))

    def _on_connect_fail(self, client, userdata) -> None:
        self._state.set_connected(False)
        LOG.warning("MQTT connection attempt to %s:%d failed; retrying with backoff",
                    self._cfg.host, self._cfg.port)

    def _on_disconnect(self, client, userdata, flags, reason_code, properties) -> None:
        self._state.set_connected(False)
        LOG.warning("MQTT disconnected (%s); automatic reconnect in progress", reason_code)

    def _on_message(self, client, userdata, msg) -> None:
        try:
            handler = self._topic_handlers.get(msg.topic)
            if handler is not None:
                handler(bytes(msg.payload), bool(msg.retain))
        except Exception:  # noqa: BLE001 - a bad message must never kill the network thread
            LOG.exception("Unexpected error handling message on %s", msg.topic)

    def _handle_distance(self, payload: bytes, retained: bool) -> None:
        value = parse_distance(payload, self._cfg.distance_min_cm, self._cfg.distance_max_cm)
        if value is None:
            LOG.warning("Ignoring invalid distance payload %r (retained=%s)", payload[:40], retained)
            return
        self._state.store_distance(value, retained)
        LOG.debug("Distance %.2f cm (retained=%s)", value, retained)

    def _handle_servo_state(self, payload: bytes, retained: bool) -> None:
        value = parse_angle(payload)
        if value is None:
            LOG.warning("Ignoring invalid servo state payload %r (retained=%s)", payload[:40], retained)
            return
        self._state.store_servo(value, retained)
        LOG.debug("Servo state %d (retained=%s)", value, retained)

    def _handle_availability(self, payload: bytes, retained: bool) -> None:
        value = parse_availability(payload)
        if value is None:
            LOG.warning("Ignoring invalid availability payload %r", payload[:40])
            return
        self._state.store_availability(value)

    # -- publishing ----------------------------------------------------------
    def publish_servo_command(self, angle: int) -> tuple[bool, str]:
        """Queue a command locally. Returns (accepted, detail); never waits for the device."""
        if not self._state.snapshot().connected:
            return False, "MQTT is not connected to the broker; command was not sent."
        try:
            info = self._client.publish(self._cfg.topic_servo_command, str(angle), qos=0, retain=False)
        except Exception as exc:  # noqa: BLE001
            LOG.exception("Publish raised")
            return False, f"Publish failed: {exc}"
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            return False, f"Publish failed: {mqtt.error_string(info.rc)}"
        LOG.info("Servo command %d published to %s", angle, self._cfg.topic_servo_command)
        return True, "Command handed to the MQTT client for delivery."


# --------------------------------------------------------------------------- #
# Response helpers
# --------------------------------------------------------------------------- #
def _iso(dt: datetime | None) -> str | None:
    return dt.isoformat(timespec="milliseconds") if dt else None


def _connection_info(snap: Snapshot) -> dict[str, Any]:
    """Connection status plus availability, not trusting cached 'online' while disconnected."""
    if snap.connected:
        availability = snap.availability or "unknown"
        note = None
    else:
        availability = "unknown"
        note = ("MQTT is disconnected, so the cached device availability cannot be trusted."
                if snap.availability else None)
    info: dict[str, Any] = {
        "mqtt_connected": snap.connected,
        "device_availability": availability,
        "last_reported_availability": snap.availability,
        "last_reported_availability_received_utc": _iso(snap.availability_utc),
    }
    if note:
        info["availability_note"] = note
    return info


def _reading_info(reading: Reading, now_mono: float) -> dict[str, Any]:
    return {
        "received_utc": _iso(reading.received_utc),
        "received_utc_meaning": "Time this server RECEIVED the MQTT message, not when it was measured/reported by the ESP32.",
        "age_seconds": round(max(0.0, now_mono - reading.received_mono), 1),
        "retained": reading.retained,
    }


# --------------------------------------------------------------------------- #
# MCP server
# --------------------------------------------------------------------------- #
def build_server(config: Config) -> FastMCP:
    state = State()
    service = MqttService(config, state)

    @asynccontextmanager
    async def lifespan(_server: FastMCP) -> AsyncIterator[None]:
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
        snap = state.snapshot()
        result: dict[str, Any] = _connection_info(snap)
        if snap.distance is None:
            result["available"] = False
            result["message"] = ("No valid distance measurement has been received yet. The ESP32 publishes "
                                 "about every 5 seconds when MQTT is connected and the reading is valid; try again shortly.")
            return result
        reading = snap.distance
        info = _reading_info(reading, snap.now_mono)
        stale = info["age_seconds"] > config.stale_after_seconds
        result.update(info)
        result.update({
            "available": True,
            "distance_cm": reading.value,
            "stale": stale,
            "stale_after_seconds": config.stale_after_seconds,
        })
        notes = []
        if reading.retained:
            notes.append("This value arrived as a RETAINED message: it is the broker's stored last value, "
                         "its original measurement time is unknown, and it may be old even though it was just received.")
        if stale:
            notes.append(f"No newer valid measurement for more than {config.stale_after_seconds:g}s; "
                         "the value may no longer reflect the current distance.")
        if not snap.connected:
            notes.append("MQTT is currently disconnected; this is the last cached value.")
        result["message"] = " ".join(notes) or "Recent distance received from the ESP32."
        return result

    @mcp.tool()
    def set_servo_angle(angle: Annotated[int, Field(strict=True)]) -> dict[str, Any]:
        """Command the ESP32 servo to an integer angle from 0 to 180 degrees (publishes over MQTT; does not confirm movement)."""
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
            "published_to": config.topic_servo_command,
            "message": ("Command published (QoS 0, not retained). This only means the local MQTT client accepted it. "
                        "It is NOT an acknowledgment from the ESP32 and does not prove the servo moved. "
                        "Call get_servo_status afterwards to see the angle the firmware reports."),
        }

    @mcp.tool()
    def get_servo_status() -> dict[str, Any]:
        """Get the last servo angle reported by the ESP32 firmware (not a physically measured position)."""
        snap = state.snapshot()
        result: dict[str, Any] = _connection_info(snap)
        if snap.servo is None:
            result["available"] = False
            result["message"] = ("No valid servo state has been received yet. The firmware reports its angle only "
                                 "after accepting a command and after each MQTT reconnection, so a long silence is normal.")
            return result
        reading = snap.servo
        result.update(_reading_info(reading, snap.now_mono))
        result.update({"available": True, "last_reported_angle": reading.value})
        notes = ["This is the last angle REPORTED by the firmware (the angle it commanded), not a measured physical position.",
                 "Age alone does not indicate a fault: the firmware only reports after commands and reconnections.",
                 "The protocol has no command IDs, so this state does not confirm that any particular command was executed."]
        if reading.retained:
            notes.append("It arrived as a RETAINED message and may be historical.")
        if not snap.connected:
            notes.append("MQTT is currently disconnected; this is the last cached value.")
        result["message"] = " ".join(notes)
        return result

    return mcp


# --------------------------------------------------------------------------- #
# Entry point
# --------------------------------------------------------------------------- #
def _resolve_config_path(argv: list[str]) -> Path:
    parser = argparse.ArgumentParser(description="Smart object MCP server (stdio)")
    parser.add_argument("--config", help=f"path to the JSON config (or set {CONFIG_ENV_VAR}); "
                                         f"default: {DEFAULT_CONFIG_PATH}")
    args, _ = parser.parse_known_args(argv)
    chosen = args.config or os.environ.get(CONFIG_ENV_VAR)
    return Path(chosen).expanduser().resolve() if chosen else DEFAULT_CONFIG_PATH


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(stream=sys.stderr, level=logging.INFO,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    try:
        config = load_config(_resolve_config_path(sys.argv[1:] if argv is None else argv))
    except ConfigError as exc:
        LOG.error("Configuration error: %s", exc)
        return 2
    logging.getLogger().setLevel(config.log_level)
    build_server(config).run(transport="stdio")
    return 0


if __name__ == "__main__":
    sys.exit(main())
