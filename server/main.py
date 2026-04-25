import logging

from fastapi import FastAPI, HTTPException, Request, WebSocket, status

from .config import settings
from .session import DeviceSession

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s: %(message)s",
)
log = logging.getLogger("companion")

app = FastAPI(title="ESP32 AI Companion Server")
sessions: dict[str, DeviceSession] = {}


@app.get("/health")
async def health() -> dict:
    return {"ok": True, "active_sessions": list(sessions.keys())}


@app.websocket("/ws/esp32/{device_id}")
async def esp32_ws(ws: WebSocket, device_id: str) -> None:
    key = ws.query_params.get("key", "")
    if key != settings.device_shared_secret:
        await ws.close(code=status.WS_1008_POLICY_VIOLATION, reason="bad key")
        return
    await ws.accept()
    log.info("[%s] websocket accepted", device_id)

    if device_id in sessions:
        log.info("[%s] replacing existing session", device_id)
        try:
            await sessions[device_id].esp.close()
        except Exception:
            pass

    session = DeviceSession(device_id, ws)
    sessions[device_id] = session
    try:
        await session.run()
    finally:
        sessions.pop(device_id, None)
        log.info("[%s] session closed", device_id)


@app.post("/upload_image/{device_id}")
async def upload_image(device_id: str, request: Request) -> dict:
    key = request.query_params.get("key", "")
    if key != settings.device_shared_secret:
        raise HTTPException(status_code=403, detail="bad key")
    session = sessions.get(device_id)
    if session is None:
        raise HTTPException(status_code=404, detail="no active session for device")
    body = await request.body()
    if not body:
        raise HTTPException(status_code=400, detail="empty body")
    if not session.deliver_image(body):
        raise HTTPException(status_code=409, detail="no image was requested")
    return {"ok": True, "bytes": len(body)}
