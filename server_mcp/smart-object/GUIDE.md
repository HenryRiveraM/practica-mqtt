# Smart Object MCP Server – Guide

MCP server (stdio) that lets an AI assistant read the ESP32 distance, command its servo and read the last servo angle the firmware reported, over MQTT. The ESP32 firmware and its MQTT contract are unchanged.

| File | Purpose |
|---|---|
| `smart-object.py` | The MCP server |
| `smart-object.config.json` | Broker, topics, thresholds, logging |
| `requirements.txt` | `mcp>=1.12,<2`, `paho-mqtt>=2.1,<3` |

> **Why `mcp<2`?** MCP SDK 2.x renamed `FastMCP` to `MCPServer` and removed `mcp.server.fastmcp`. This server uses the 1.x `FastMCP` API, so the dependency is capped below 2. Requires Python 3.11+.

## 1. Install

Windows (PowerShell), from the project folder:

```powershell
cd "C:\Users\cae_i\Desktop\UCB 2026\2 - 2026\Internet de las cosas 2 - 2026\practica-mqtt\server_mcp\smart-object"
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

macOS/Linux:

```bash
python3 -m venv .venv && ./.venv/bin/python -m pip install -r requirements.txt
```

## 2. Configuration

Edit `smart-object.config.json`. By default it is read from the folder containing `smart-object.py` (works from any working directory). Override with either:

```powershell
.\.venv\Scripts\python.exe smart-object.py --config C:\path\to\other.json
$env:SMART_OBJECT_CONFIG = "C:\path\to\other.json"
```

`--config` wins over the environment variable. Invalid configs print an actionable error to stderr and exit with code 2.

| Key | Default | Notes |
|---|---|---|
| `mqtt.host` / `port` | `broker.hivemq.com` / `1883` | Plain TCP, no auth |
| `mqtt.client_id_prefix` | `smart-object-mcp-` | A UUID is appended per process |
| `mqtt.keepalive` | `30` | seconds |
| `mqtt.reconnect_min_delay` / `max_delay` | `1` / `60` | Bounded exponential backoff (paho rounds up to whole seconds) |
| `topics.*` | `sis234/grupo9/...` | `distance`, `servo_command`, `servo_state`, `availability` |
| `distance.stale_after_seconds` | `15` | |
| `distance.min_cm` / `max_cm` | `2` / `400` | |
| `logging.level` | `INFO` | Logs go to **stderr** only |

## 3. Run and inspect

Run directly (waits for MCP messages on stdin; logs on stderr):

```powershell
.\.venv\Scripts\python.exe smart-object.py
```

MCP Inspector (needs Node.js):

```powershell
npx @modelcontextprotocol/inspector "C:\Users\cae_i\Desktop\UCB 2026\2 - 2026\Internet de las cosas 2 - 2026\practica-mqtt\server_mcp\smart-object\.venv\Scripts\python.exe" "C:\Users\cae_i\Desktop\UCB 2026\2 - 2026\Internet de las cosas 2 - 2026\practica-mqtt\server_mcp\smart-object\smart-object.py"
```

Open the URL it prints, **Connect**, then **Tools → List Tools**.

## 4. Register in MCP clients

Always use absolute paths, including the venv's Python.

### Claude Desktop

Open *Settings → Developer → Edit Config*. The file is `%APPDATA%\Claude\claude_desktop_config.json` (Windows) or `~/Library/Application Support/Claude/claude_desktop_config.json` (macOS). Add (JSON needs doubled backslashes):

```json
{
  "mcpServers": {
    "smart-object": {
      "command": "C:\\Users\\cae_i\\Desktop\\UCB 2026\\2 - 2026\\Internet de las cosas 2 - 2026\\practica-mqtt\\server_mcp\\smart-object\\.venv\\Scripts\\python.exe",
      "args": [
        "C:\\Users\\cae_i\\Desktop\\UCB 2026\\2 - 2026\\Internet de las cosas 2 - 2026\\practica-mqtt\\server_mcp\\smart-object\\smart-object.py"
      ]
    }
  }
}
```

Optionally add `"--config", "<absolute path>"` to `args`. Fully quit and restart Claude Desktop. Logs: `%APPDATA%\Claude\logs\mcp-server-smart-object.log`.

### Codex

CLI:

```powershell
codex mcp add smart-object -- "C:\Users\cae_i\Desktop\UCB 2026\2 - 2026\Internet de las cosas 2 - 2026\practica-mqtt\server_mcp\smart-object\.venv\Scripts\python.exe" "C:\Users\cae_i\Desktop\UCB 2026\2 - 2026\Internet de las cosas 2 - 2026\practica-mqtt\server_mcp\smart-object\smart-object.py"
codex mcp list
```

Or edit `~/.codex/config.toml` (TOML literal strings avoid escaping backslashes):

```toml
[mcp_servers.smart-object]
command = 'C:\Users\cae_i\Desktop\UCB 2026\2 - 2026\Internet de las cosas 2 - 2026\practica-mqtt\server_mcp\smart-object\.venv\Scripts\python.exe'
args = ['C:\Users\cae_i\Desktop\UCB 2026\2 - 2026\Internet de las cosas 2 - 2026\practica-mqtt\server_mcp\smart-object\smart-object.py']
startup_timeout_sec = 20
```

(Claude Desktop and Codex instructions checked on 2026-10-25 against modelcontextprotocol.io "Connect to local MCP servers" and the Codex MCP docs. Both products change quickly; re-check if something differs.)

### Prompts to try

- "What is the current distance detected by my ESP32?" → `get_distance`
- "Move the servo motor to 90 degrees." / "…to 180 degrees." → `set_servo_angle`
- "What is the last servo angle reported by my ESP32?" → `get_servo_status`

## 5. Behavior to know

- Timestamps are **receipt time at this server**, not measurement time. Ages use a monotonic clock.
- A **retained** value was stored by the broker; its original time is unknown even if just received. Note the broker only sets the retained flag on messages delivered *at subscribe time*; a retained message published while this server is already connected arrives with `retained=false`.
- `set_servo_angle` success = the local MQTT client accepted a QoS 0, non-retained publish. It is not an ESP32 acknowledgment. Check `get_servo_status` afterwards; with no command IDs, even that cannot prove a specific command ran, and the servo has no position sensor.
- Cached `online` is reported as `unknown` while MQTT is disconnected (the cached value remains visible as `last_reported_availability`).
- `set_servo_angle` uses strict integer validation: `true`, `"90"`, and `90.5` are rejected by schema validation (a tool error); `-1`/`181` return `success:false` with an error.
- Topics are shared by group 9 and not per-device; other clients publishing there will affect results.

## 6. Test procedure

### Use alternate topics for simulation

Publishing fake values to `sis234/grupo9/...` is visible to **every** client of the shared topics (teammates' dashboards, the real ESP32 will move its servo on a real command!). For simulation, copy the config and change all four topics:

```powershell
Copy-Item smart-object.config.json test.config.json
# edit test.config.json: replace "sis234/grupo9" with "sis234/grupo9-test-<yourname>" in all four topics
.\.venv\Scripts\python.exe smart-object.py --config test.config.json
```

The examples below use `T=sis234/grupo9-test-me` for simulated runs. Everything published this way is **simulated**, not real ESP32 telemetry. To test against the real device use `T=sis234/grupo9` (only observe, or send commands you intend the real servo to execute).

Requires the Mosquitto clients (`mosquitto_pub`, `mosquitto_sub`).

```bash
T=sis234/grupo9-test-me
# Watch commands the server publishes (use the real topics to see real traffic)
mosquitto_sub -h broker.hivemq.com -p 1883 -v -t "$T/servo/cmd"
# Real project topics, passive observation of the real ESP32:
mosquitto_sub -h broker.hivemq.com -p 1883 -v -t "sis234/grupo9/#"
```

PowerShell: use `$T = "sis234/grupo9-test-me"` and `"$T/servo/cmd"`.

| # | Test | Steps | Expected |
|---|---|---|---|
| 1 | Distance before data | Start server with fresh test topics (no retained data); call `get_distance` | `available:false` with an explanatory message |
| 2 | Valid distance | `mosquitto_pub -h broker.hivemq.com -t "$T/distancia" -m "23.45"` then `get_distance` | `distance_cm: 23.45`, small `age_seconds`, `stale:false` |
| 3 | Malformed / out of range | publish `abc`, `nan`, `inf`, `1.99`, `400.01`, empty (`-m ""`), `-m "23,4"` | Warnings in stderr; `get_distance` still returns 23.45 |
| 4 | Stale | Wait longer than `stale_after_seconds` (or set it to 3) | `stale:true` with note |
| 5 | Retained | `mosquitto_pub -h broker.hivemq.com -t "$T/distancia" -m "50.00" -r`, then **restart the server** | `retained:true`, message says original measurement time unknown. Clear it afterwards: `mosquitto_pub -h broker.hivemq.com -t "$T/distancia" -n -r` |
| 6 | Servo commands | With the `mosquitto_sub` on `$T/servo/cmd` running, ask for 90, then 180 | `success:true`; subscriber shows `90` then `180`, not retained; message states it's not an acknowledgment |
| 7 | Rejections | Call with `-1`, `181`, `90.5`, `true` | Errors; nothing appears on `$T/servo/cmd` |
| 8 | Servo status | Before: `get_servo_status` → `available:false`. Then `mosquitto_pub -h broker.hivemq.com -t "$T/servo/estado" -m "180"` | `last_reported_angle:180`; `set_servo_angle` itself never changes this value |
| 9 | Availability | Publish `online` to `$T/estado`; `get_distance` shows `online`. Then break MQTT (test 10) | `device_availability:"unknown"` while disconnected |
| 10 | Connection failure | Set `mqtt.host` to `127.0.0.1`, `port` to `1` | stderr shows failed attempts with backoff; tools still respond with `mqtt_connected:false`; `set_servo_angle` returns `success:false` "MQTT is not connected" |
| 11 | Broker loss / reconnect | Connect normally, then disable the network adapter (or block port 1883) for ~1 min, re-enable | stderr: "MQTT disconnected…", later "MQTT connected…"; tools keep answering throughout; after reconnect new messages are received (subscriptions renewed) |
| 12 | Independence | Publish distances every second while sending servo commands | Distances keep updating; no command is ever published by receiving distance; servo status changes only via `servo/estado` messages |

Test 3 payloads in one go:

```bash
for p in abc nan inf 1.99 400.01 ""; do mosquitto_pub -h broker.hivemq.com -t "$T/distancia" -m "$p"; done
```

## 7. Troubleshooting

- Server not listed in the client: verify absolute paths and JSON/TOML syntax, restart the client, read its MCP log.
- `No module named 'mcp.server.fastmcp'`: you installed mcp 2.x; reinstall with `pip install -r requirements.txt`.
- Anything printed to stdout corrupts the protocol; this server logs only to stderr.
- Public broker (HiveMQ) connects can take a couple of seconds; call tools after the "MQTT connected" log line.
