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
