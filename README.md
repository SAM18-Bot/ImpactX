# ImpactX

ImpactX is a hybrid emergency response system that combines an edge device (ESP32 + sensors + GSM) with a FastAPI backend and web dashboard.

The design prioritizes reliability: critical emergency actions can still execute at the edge even when cloud connectivity is unavailable.

## Key Features

- Hybrid edge/cloud architecture with fail-safe behavior.
- Real-time severity classification: `SAFE`, `ALERT`, `EMERGENCY`.
- 20-second confirmation window before escalation.
- Local emergency handling on device (buzzer, LED, GSM SMS).
- Dashboard for live status, activity feed, and event logs.
- Optional Twilio integration for cloud SMS/call workflows.

## Architecture

### Edge Layer (ESP32 + MPU6050 + GPS + SIM800L)

- Reads telemetry and computes local severity.
- Attempts delivery to backend.
- If backend is unavailable, continues in offline mode.
- Starts physical cancel window.
- Triggers local emergency outputs and GSM notification when required.
- Buffers failed GSM sends for retry.

### Cloud Layer (FastAPI)

- Validates and normalizes incoming events.
- Computes severity and manages event state transitions.
- Resolves nearest hospital from a demo dataset.
- Sends communication actions (simulated or Twilio-backed).
- Persists event records for audit/history.

## Repository Structure

- `main.py` — FastAPI backend and agent workflow.
- `static/index.html` — dashboard UI.
- `iot/esp32_sender.ino` — ESP32 firmware logic.
- `events_log.jsonl` — runtime event log output.

## API Endpoints

- `POST /event` — ingest telemetry event.
- `POST /event/{event_id}/cancel` — cancel pending event.
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

## Dashboard Demo (No Hardware Required)

The dashboard supports manual simulation buttons:

- `Demo SAFE`
- `Demo ALERT`
- `Demo EMERGENCY`

You can also submit a location-based sample event using `Send Demo Event (My Location)`.

## Twilio Integration (Optional)

By default, communication actions are simulated.

Enable real Twilio delivery:

```bash
export ENABLE_REAL_COMMUNICATION=true
export TWILIO_ACCOUNT_SID=ACxxxxxxxxxxxxxxxxxxxxxxxxxxxx
export TWILIO_AUTH_TOKEN=xxxxxxxxxxxxxxxxxxxxxxxxxxxx
export TWILIO_FROM_NUMBER=+1XXXXXXXXXX
export EMERGENCY_TO_NUMBER=+1YYYYYYYYYY
export TWILIO_TWIML_URL=http://demo.twilio.com/docs/voice.xml
```

Delivery behavior:

- `ALERT` → SMS
- `EMERGENCY` → SMS + voice call
