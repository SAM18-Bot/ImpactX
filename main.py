from __future__ import annotations

import asyncio
import json
import math
import os
from urllib.parse import urlencode
from urllib.request import Request, urlopen
from dataclasses import dataclass, asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Literal

from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

app = FastAPI(title="ImpactX Emergency Response", version="1.0.0")

LOG_FILE = Path("events_log.jsonl")


class SensorEvent(BaseModel):
    impact: float = Field(..., ge=0, le=50)
    tilt: float = Field(..., ge=0, le=180)
    speed: float = Field(..., ge=0, le=300)
    lat: float = Field(..., ge=-90, le=90)
    lon: float = Field(..., ge=-180, le=180)
    timestamp: datetime


@dataclass
class AgentActivity:
    agent: str
    message: str
    ts: str


class EventResult(BaseModel):
    event_id: int
    status: Literal["SAFE", "ALERT", "EMERGENCY", "PENDING_CONFIRMATION", "CANCELLED"]
    score: float
    hospital: dict[str, Any] | None = None
    location_link: str
    created_at: str
    activities: list[dict[str, str]]


state: dict[str, Any] = {
    "latest": None,
    "logs": [],
    "activities": [],
    "next_id": 1,
}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def add_activity(agent: str, message: str) -> AgentActivity:
    activity = AgentActivity(agent=agent, message=message, ts=utc_now())
    state["activities"].append(asdict(activity))
    # Keep recent activity only for dashboard readability
    state["activities"] = state["activities"][-50:]
    return activity


# -----------------------------
# Agent implementations
# -----------------------------
def perception_agent(raw: SensorEvent) -> dict[str, Any]:
    normalized_speed = min(raw.speed / 120.0, 1.0)
    normalized_impact = min(raw.impact / 10.0, 1.0)
    normalized_tilt = min(raw.tilt / 90.0, 1.0)

    cleaned = {
        "impact": raw.impact,
        "tilt": raw.tilt,
        "speed": raw.speed,
        "lat": round(raw.lat, 6),
        "lon": round(raw.lon, 6),
        "timestamp": raw.timestamp.astimezone(timezone.utc).isoformat(),
        "norm": {
            "impact": round(normalized_impact, 3),
            "speed": round(normalized_speed, 3),
            "tilt": round(normalized_tilt, 3),
        },
    }
    add_activity("Perception Agent", "Data received and normalized")
    return cleaned


def calculate_severity(event: dict[str, Any]) -> tuple[float, str]:
    score = (event["impact"] * 0.5) + (event["speed"] * 0.3) + (event["tilt"] * 0.2)
    if score < 30:
        status = "SAFE"
    elif score <= 70:
        status = "ALERT"
    else:
        status = "EMERGENCY"
    add_activity("Decision Agent", f"Score computed: {score:.2f}, status={status}")
    return round(score, 2), status


def coordination_agent(lat: float, lon: float) -> dict[str, Any]:
    # Mock hospitals around Pune coordinates for demo purposes.
    hospitals = [
        {"name": "City Hospital", "lat": 18.530, "lon": 73.850},
        {"name": "General Medical Center", "lat": 18.515, "lon": 73.870},
        {"name": "Sunrise Trauma Care", "lat": 18.525, "lon": 73.840},
    ]

    def dist_km(a_lat: float, a_lon: float, b_lat: float, b_lon: float) -> float:
        # quick Haversine
        r = 6371
        dlat = math.radians(b_lat - a_lat)
        dlon = math.radians(b_lon - a_lon)
        x = (
            math.sin(dlat / 2) ** 2
            + math.cos(math.radians(a_lat))
            * math.cos(math.radians(b_lat))
            * math.sin(dlon / 2) ** 2
        )
        return r * 2 * math.atan2(math.sqrt(x), math.sqrt(1 - x))

    closest = min(
        hospitals,
        key=lambda h: dist_km(lat, lon, h["lat"], h["lon"]),
    )
    distance = dist_km(lat, lon, closest["lat"], closest["lon"])
    result = {
        "hospital": closest["name"],
        "distance": f"{distance:.2f} km",
    }
    add_activity("Coordination Agent", f"Nearest hospital: {result['hospital']} ({result['distance']})")
    return result


def communication_agent(lat: float, lon: float, status: str) -> str:
    msg = (
        f"Accident detected! Status: {status}. "
        f"Location: https://maps.google.com/?q={lat},{lon}"
    )
    sms_ok = send_sms(msg)
    call_ok = False
    if status == "EMERGENCY":
        call_ok = make_emergency_call(lat, lon)
    mode = "real" if sms_ok or call_ok else "simulated"
    add_activity("Communication Agent", f"Alert sent ({mode}; sms={sms_ok}, call={call_ok})")
    # Simulated SMS/Twilio action
    print(f"[Communication Agent] SMS simulated: {msg}")
    add_activity("Communication Agent", "Alert sent (simulated SMS)")
    return msg


def learning_agent(record: dict[str, Any]) -> None:
    LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    with LOG_FILE.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record) + "\n")
    add_activity("Learning Agent", f"Event {record['event_id']} stored")


def _twilio_env_ready() -> bool:
    required = [
        "TWILIO_ACCOUNT_SID",
        "TWILIO_AUTH_TOKEN",
        "TWILIO_FROM_NUMBER",
        "EMERGENCY_TO_NUMBER",
    ]
    return os.getenv("ENABLE_REAL_COMMUNICATION", "false").lower() == "true" and all(os.getenv(k) for k in required)


def _twilio_post(path: str, data: dict[str, str]) -> bool:
    sid = os.getenv("TWILIO_ACCOUNT_SID", "")
    token = os.getenv("TWILIO_AUTH_TOKEN", "")
    payload = urlencode(data).encode("utf-8")
    req = Request(
        f"https://api.twilio.com/2010-04-01/Accounts/{sid}/{path}.json",
        data=payload,
        method="POST",
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )
    import base64

    auth = base64.b64encode(f"{sid}:{token}".encode("utf-8")).decode("utf-8")
    req.add_header("Authorization", f"Basic {auth}")
    try:
        with urlopen(req, timeout=10) as resp:
            return 200 <= resp.status < 300
    except Exception as exc:
        add_activity("Communication Agent", f"Twilio request failed: {exc}")
        return False


def send_sms(message: str) -> bool:
    if not _twilio_env_ready():
        print(f"[Communication Agent] SMS simulated: {message}")
        return False
    return _twilio_post(
        "Messages",
        {
            "From": os.getenv("TWILIO_FROM_NUMBER", ""),
            "To": os.getenv("EMERGENCY_TO_NUMBER", ""),
            "Body": message,
        },
    )


def make_emergency_call(lat: float, lon: float) -> bool:
    if not _twilio_env_ready():
        print(f"[Communication Agent] Emergency call simulated for {lat},{lon}")
        return False
    twiml_url = os.getenv("TWILIO_TWIML_URL", "http://demo.twilio.com/docs/voice.xml")
    return _twilio_post(
        "Calls",
        {
            "From": os.getenv("TWILIO_FROM_NUMBER", ""),
            "To": os.getenv("EMERGENCY_TO_NUMBER", ""),
            "Url": twiml_url,
        },
    )


async def finalize_event_after_countdown(event_id: int, event: dict[str, Any], score: float, tentative: str) -> None:
    add_activity("Decision Agent", f"Waiting 10 seconds for user override (event {event_id})")
    await asyncio.sleep(10)

    latest = state.get("latest")
    if not latest or latest.get("event_id") != event_id:
        return
    if latest.get("status") == "CANCELLED":
        add_activity("Decision Agent", f"Event {event_id} was cancelled by user")
        return

    final_status = tentative
    hospital = None
    if final_status in {"ALERT", "EMERGENCY"}:
        hospital = coordination_agent(event["lat"], event["lon"])
        communication_agent(event["lat"], event["lon"], final_status)

    result = {
        "event_id": event_id,
        "status": final_status,
        "score": score,
        "hospital": hospital,
        "location_link": f"https://maps.google.com/?q={event['lat']},{event['lon']}",
        "created_at": utc_now(),
        "activities": list(state["activities"]),
    }
    state["latest"] = result
    state["logs"].append(result)
    learning_agent(result)


@app.post("/event", response_model=EventResult)
async def ingest_event(sensor_event: SensorEvent):
    event = perception_agent(sensor_event)
    score, status = calculate_severity(event)

    event_id = state["next_id"]
    state["next_id"] += 1

    pending_status: EventResult = EventResult(
        event_id=event_id,
        status="PENDING_CONFIRMATION" if status in {"ALERT", "EMERGENCY"} else "SAFE",
        score=score,
        hospital=None,
        location_link=f"https://maps.google.com/?q={event['lat']},{event['lon']}",
        created_at=utc_now(),
        activities=list(state["activities"]),
    )

    state["latest"] = pending_status.model_dump()

    if status == "SAFE":
        safe_record = pending_status.model_dump()
        safe_record["status"] = "SAFE"
        state["latest"] = safe_record
        state["logs"].append(safe_record)
        learning_agent(safe_record)
        return EventResult(**safe_record)

    asyncio.create_task(finalize_event_after_countdown(event_id, event, score, status))
    return pending_status


@app.post("/event/{event_id}/cancel")
async def cancel_event(event_id: int):
    latest = state.get("latest")
    if not latest or latest.get("event_id") != event_id:
        raise HTTPException(status_code=404, detail="Event not found")
    if latest.get("status") != "PENDING_CONFIRMATION":
        raise HTTPException(status_code=400, detail="Only pending events can be cancelled")

    latest["status"] = "CANCELLED"
    state["logs"].append(dict(latest))
    learning_agent(dict(latest))
    return {"ok": True, "event_id": event_id, "status": "CANCELLED"}


@app.get("/status")
def get_status():
    return {
        "latest": state.get("latest"),
        "activity": state.get("activities", [])[-10:],
    }


@app.get("/logs")
def get_logs():
    return {"count": len(state["logs"]), "events": state["logs"]}


@app.get("/health")
def health():
    return {"ok": True, "time": utc_now()}


if Path("static").exists():
    app.mount("/static", StaticFiles(directory="static"), name="static")


@app.get("/", response_class=HTMLResponse)
def dashboard():
    dashboard_file = Path("static/index.html")
    if dashboard_file.exists():
        return dashboard_file.read_text(encoding="utf-8")
    return "<h1>ImpactX Dashboard not found</h1>"
