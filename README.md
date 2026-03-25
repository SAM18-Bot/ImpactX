# ImpactX – Autonomous Emergency Response Agent

A multi-agent FastAPI prototype implementing the **Perception → Reasoning → Action → Learning** loop for crash-response automation.

## Architecture

- **Perception Agent**: validates + normalizes incoming sensor payloads from IoT devices.
- **Decision Agent**: computes severity score and classifies into `SAFE`, `ALERT`, or `EMERGENCY`.
- **Coordination Agent**: finds nearest hospital (mock dataset).
- **Communication Agent**: sends emergency alert (simulated SMS log).
- **Learning Agent**: persists all finalized events into `events_log.jsonl`.

## API

- `POST /event` – receive telemetry and trigger workflow.
- `POST /event/{event_id}/cancel` – optional user override during 10-second countdown.
- `GET /status` – latest state + agent activity feed.
- `GET /logs` – event history.
- `GET /` – dashboard.

## Run

```bash
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn main:app --reload
```

Open <http://127.0.0.1:8000> for the dashboard.

### Demo with your current location

1. Open the dashboard in a browser.
2. Allow location permission when prompted.
3. Click **Send Demo Event (My Location)**.
4. Dashboard will post an event using your browser coordinates.

> Note: Browser geolocation requires permission and may be blocked in some environments.

## Example event payload

```json
{
  "impact": 8.5,
  "tilt": 60,
  "speed": 72,
  "lat": 18.5204,
  "lon": 73.8567,
  "timestamp": "2026-03-27T10:30:00Z"
}
```

## Emergency SMS + Calling (Twilio)

By default, this project **simulates** SMS/calls in logs for demo use.  
To enable real communication, set these environment variables before starting FastAPI:

```bash
export ENABLE_REAL_COMMUNICATION=true
export TWILIO_ACCOUNT_SID=ACxxxxxxxxxxxxxxxxxxxxxxxxxxxx
export TWILIO_AUTH_TOKEN=xxxxxxxxxxxxxxxxxxxxxxxxxxxx
export TWILIO_FROM_NUMBER=+1XXXXXXXXXX
export EMERGENCY_TO_NUMBER=+1YYYYYYYYYY
# optional for voice calls:
export TWILIO_TWIML_URL=http://demo.twilio.com/docs/voice.xml
```

Behavior:
- `ALERT` → sends SMS
- `EMERGENCY` → sends SMS + places a call
