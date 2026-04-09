# ImpactX

ImpactX is an emergency response system that combines an edge device (ESP32 + sensors + GPS) with a FastAPI backend and web dashboard.

The ESP32 sends telemetry to the backend, and the backend handles severity decisions, logging, and optional Twilio notifications.

## Key Features

- ESP32 + FastAPI architecture for live incident telemetry.
- Real-time severity classification: `SAFE`, `ALERT`, `EMERGENCY`.
- 20-second confirmation window before escalation.
- ESP32 sends high-impact sensor/GPS telemetry for cloud decisioning.
- Local emergency indication on device (red LED + buzzer) with green LED always on.
- Dashboard for live status, activity feed, and event logs.
- Optional Twilio integration for cloud SMS/call workflows.
- Online learning from cancelled incidents (false alarms) adjusts thresholds.

## Architecture

### Edge Layer (ESP32 + MPU6050 + GPS)

- Reads telemetry and computes local severity.
- If backend is unavailable, keeps operating standalone with local cancel/alert logic.
- If backend is available, sends only high-impact events (`impact > 20`) with telemetry.
- Starts physical cancel window.
- Polls backend command channel and executes remote `ALERT`/`EMERGENCY`/`CANCEL` actions.
- Triggers local emergency outputs when required.

### Cloud Layer (FastAPI)

- Validates and normalizes incoming events.
- Computes severity and manages event state transitions.
- Resolves nearest hospital from a demo dataset.
- Sends communication actions (simulated or Twilio-backed).
- Persists event records for audit/history.

## Project Workflow

### 1) Data Ingestion
- Hardware mode: ESP32 sends sensor payload to `POST /event`.
- Demo mode: dashboard buttons submit predefined `SAFE`, `ALERT`, or `EMERGENCY` payloads to `POST /event`.

### 2) Perception + Decision
- Backend normalizes telemetry and computes severity score using:
  - `impact_score = min(100, impact * 6.5)`
  - `speed_score = min(100, speed / 140 * 100)`
  - `tilt_score = min(100, tilt / 90 * 100)`
  - `risk_score = impact_score*0.55 + speed_score*0.30 + tilt_score*0.15`
  - low-speed suppression (`speed < 12 && impact < 7`) applies `risk_score *= 0.65`
- Classification (adaptive):
  - `SAFE` if `risk_score < (20 + shift)`
  - `ALERT` if `20 + shift <= risk_score <= 50 + shift`
  - `EMERGENCY` if `risk_score > 50 + shift`
- `shift` is learned online from false-alarm ratio (cancelled incidents).

### 3) Confirmation Window
- `ALERT` and `EMERGENCY` first enter `PENDING_CONFIRMATION`.
- System waits 20 seconds for manual cancel via `POST /event/{event_id}/cancel`.

### 4) Escalation Path
- If cancelled: status becomes `CANCELLED`.
- If not cancelled:
  - `ALERT` → hospital lookup + SMS flow.
  - `EMERGENCY` → hospital lookup + SMS + voice call flow.

### 5) Communication Behavior
- Default behavior is simulated communication (safe for local demos).
- Real delivery is enabled only when `ENABLE_REAL_COMMUNICATION=true` and Twilio environment variables are configured.

### 6) Persistence + Monitoring
- Finalized events are appended to `events_log.jsonl`.
- Adaptive learning profile is persisted in `learning_profile.json`.
- Dashboard polls:
  - `GET /status` for latest state and recent activity.
  - `GET /logs` for full event history (UI filters to ALERT/EMERGENCY rows).

## Repository Structure

- `main.py` — FastAPI backend and agent workflow.
- `static/index.html` — dashboard UI.
- `iot/esp32_sender.ino` — ESP32 firmware logic.
- `events_log.jsonl` — runtime event log output.

## API Endpoints

- `POST /event` — ingest telemetry event (JSON).
- `POST /event/{event_id}/cancel` — cancel pending event.
- `POST /device/report` — device heartbeat + hardware false-alarm cancellation report.
- `GET /device/{device_id}/command` — fetch latest backend command for ESP32.
- `POST /device/{device_id}/command/ack` — acknowledge command execution.
- `GET /status` — latest status and recent activity.
- `GET /logs` — event history.
- `GET /health` — service health check.
- `GET /` — dashboard page.

## Local Setup

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn main:app --reload
```

Dashboard: <http://127.0.0.1:8000>

## Connect ESP32 to Backend (Receive Live Hardware Data)

1. **Run backend on a reachable network interface**
   ```bash
   uvicorn main:app --host 0.0.0.0 --port 8000 --reload
   ```

2. **Find your laptop/PC LAN IP**
   - Example: `192.168.1.23`
   - ESP32 and laptop must be on the same Wi-Fi network.

3. **Update ESP32 firmware config in `iot/esp32_sender.ino`**
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI";
   const char* WIFI_PASS = "YOUR_PASS";
   const char* API_URL = "http://192.168.1.23:8000/event/camera";
   ```
   Use your actual LAN IP in `API_URL` (not `127.0.0.1`).

4. **Flash ESP32 and open Serial Monitor (115200)**
   - On successful posts you should see: `POST /event try X => 200`.
   - If backend is unreachable, device retries and logs POST failures in Serial Monitor.

5. **Verify data in backend dashboard**
   - Open: `http://<your-laptop-ip>:8000` (or `http://127.0.0.1:8000` on same machine).
   - Check `GET /status` and `GET /logs` for incoming events.

### Quick troubleshooting
- `POST ... => -1` or timeout: wrong `API_URL`, network mismatch, or firewall blocking port `8000`.
- No Wi-Fi connect on ESP32: wrong SSID/password or unsupported band (use 2.4 GHz).
- Dashboard opens but no events: ensure sensor score crosses threshold so firmware sends `/event`.
- Firmware sends only high-impact candidates (`impact > 20`) to reduce low-impact network noise.

## Dashboard Demo (No Hardware Required)

The dashboard supports manual simulation buttons:

- `Demo SAFE`
- `Demo ALERT`
- `Demo EMERGENCY`

You can also submit a location-based sample event using `Send Demo Event (My Location)`.

## Twilio Integration (Optional)

By default, communication actions are simulated.

### Where to add Twilio credentials

Set these environment variables in the same terminal/session where you start the FastAPI server (`uvicorn main:app --reload`), or configure them in your deployment platform's environment settings.

Enable real Twilio delivery:

```bash
export ENABLE_REAL_COMMUNICATION=true
export TWILIO_ACCOUNT_SID=ACxxxxxxxxxxxxxxxxxxxxxxxxxxxx
export TWILIO_AUTH_TOKEN=xxxxxxxxxxxxxxxxxxxxxxxxxxxx
export TWILIO_FROM_NUMBER=+1XXXXXXXXXX
export EMERGENCY_TO_NUMBER=+1YYYYYYYYYY
export TWILIO_TWIML_URL=http://demo.twilio.com/docs/voice.xml
```

### What ImpactX sends through Twilio

- `SAFE`:
  - No Twilio SMS or call is sent.
- `ALERT`:
  - Sends one SMS to `EMERGENCY_TO_NUMBER`.
- `EMERGENCY`:
  - Sends one SMS to `EMERGENCY_TO_NUMBER`.
  - Places one voice call to `EMERGENCY_TO_NUMBER`.

Current SMS body format:

```text
Accident detected! Status: <ALERT|EMERGENCY>. Location: https://maps.google.com/?q=<lat>,<lon>
```

Current voice call behavior:

- Uses Twilio `Calls` API from `TWILIO_FROM_NUMBER` to `EMERGENCY_TO_NUMBER`.
- Plays TwiML from `TWILIO_TWIML_URL` (defaults to Twilio demo URL if not set).

Delivery behavior:

- `ALERT` → SMS
- `EMERGENCY` → SMS + voice call
