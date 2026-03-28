# ImpactX – Autonomous Emergency Response Agent

ImpactX now uses a **hybrid fail-safe architecture (Edge + Cloud)** so critical emergency actions can still happen when internet or backend services fail.

## Hybrid Architecture (Dual Decision System)

- **Edge Layer (ESP32 + sensors + SIM800L)**
  - Performs immediate local severity estimation.
  - Starts a **20-second physical cancel window** using a hardware button.
  - If not cancelled, triggers local emergency actions (buzzer + LED + GSM SMS).
  - Retries cloud delivery and buffers unsent GSM alerts for eventual retry.
- **Cloud Layer (FastAPI multi-agent backend)**
  - Runs advanced workflow, activity feed, event logging, dashboard visibility, hospital lookup, and optional Twilio comms.

This means the system is **fail-safe, not cloud-dependent** for life-critical response.

## Agent Design (Cloud)

- **Perception Agent**: validates + normalizes incoming sensor payloads.
- **Decision Agent**: computes severity score and classifies `SAFE`/`ALERT`/`EMERGENCY`.
- **Coordination Agent**: finds nearest hospital (mock dataset).
- **Communication Agent**: sends Twilio SMS/call or simulates in demo mode.
- **Learning Agent**: persists finalized events into `events_log.jsonl`.

## API

- `POST /event` – receive telemetry and trigger workflow.
- `POST /event/{event_id}/cancel` – cloud-side manual override during pending state.
- `GET /status` – latest state + recent agent activity.
- `GET /logs` – event history.
- `GET /health` – liveness probe.
- `GET /` – web dashboard.

## Run (Backend)

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn main:app --reload
```

Open <http://127.0.0.1:8000> for the dashboard.

## Edge Fail-Safe Behavior (ESP32)

When crash score crosses threshold, ESP32 now does all of this locally:

1. Retry sending event to backend (up to 5 times).
2. If backend unreachable, switch to **offline edge mode**.
3. Start **20-second countdown**.
4. Allow user to cancel with **physical button** on device.
5. If not cancelled, trigger **buzzer + LED + GSM SMS**.
6. If GSM send fails, keep message in buffer and retry later.

## Dashboard Demo

1. Open dashboard in browser.
2. Allow geolocation permission.
3. Click **Send Demo Event (My Location)**.
4. Watch status + activity feed update.

> Browser geolocation depends on permission and browser support.

## Twilio (Optional Cloud Comms)

By default, backend comms are simulated for demo.

```bash
export ENABLE_REAL_COMMUNICATION=true
export TWILIO_ACCOUNT_SID=ACxxxxxxxxxxxxxxxxxxxxxxxxxxxx
export TWILIO_AUTH_TOKEN=xxxxxxxxxxxxxxxxxxxxxxxxxxxx
export TWILIO_FROM_NUMBER=+1XXXXXXXXXX
export EMERGENCY_TO_NUMBER=+1YYYYYYYYYY
export TWILIO_TWIML_URL=http://demo.twilio.com/docs/voice.xml
```

Behavior:
- `ALERT` → SMS
- `EMERGENCY` → SMS + call
