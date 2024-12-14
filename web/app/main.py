#!/usr/bin/env python3

if __name__ == "__main__":
    import sys
    print("run using uvicorn instead")
    sys.exit()

import asyncio
from contextlib import asynccontextmanager
import datetime
import json
import os
from pathlib import Path
import secrets
import time
from typing import Any
from fastapi import Depends, FastAPI, HTTPException, Response, WebSocket
import jwt
from websockets.exceptions import ConnectionClosedOK
from fastapi.responses import HTMLResponse
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from pydantic import BaseModel
from google.oauth2 import id_token
from google.auth.transport import requests
from app.manager import Esp32Manager
import redis.asyncio as redis
import logging

log = logging.getLogger("main")



class WebConf(BaseModel):
    email_whitelist: set[str]
    jwt_secret: bytes
    google_expiration: float
    invite_expiration: float
    invite_span: float


with open("./conf-web.json", 'r') as file:
    conf = WebConf.model_validate_json(file.read())
index_html = Path(__file__).with_name("index.html").read_text()
service_worker_js = Path(__file__).with_name("service-worker.js").read_bytes()
favicon_png = Path("./logo_doorctl.png").read_bytes()


red: redis.Redis = None


manager: Esp32Manager = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global manager, red
    logging.basicConfig(
        level=os.environ.get('LOGLEVEL', 'INFO').upper(),
    )
    red = redis.Redis(host="doorctl-redis")
    manager = Esp32Manager()
    try:
        yield
    finally:
        await manager.close()
        await red.close()


app = FastAPI(lifespan=lifespan)

CLIENT_ID = "200671129298-58096itq8l7g8uskluhqbte5asl0dbv2.apps.googleusercontent.com"


@app.get("/")
async def root():
    return HTMLResponse(index_html)


@app.get('/service-worker.js')
async def service_worker():
    return Response(content=service_worker_js, media_type="text/javascript")


@app.get('/favicon.ico', include_in_schema=False)
async def favicon():
    return Response(content=favicon_png, media_type="image/png")


async def authorize_google(google_token: str) -> str:
    try:
        userdata = id_token.verify_oauth2_token(google_token, requests.Request(), CLIENT_ID)
        email = userdata['email']
    except ValueError:
        raise HTTPException(status_code=401)
    if email not in conf.email_whitelist:
        raise HTTPException(status_code=403, detail="Not in whitelist")
    return email


async def authorize(token: str, need_invite: bool = False, destructive: bool = True) -> dict[str, Any]:
    if token[:1] == "u" and len(token[1:]) < 50:
        # Use key
        usekey = token[1:]
        payload_str = await red.get(f"usekey:{usekey}")
        if payload_str is None:
            raise HTTPException(status_code=401)
        payload = json.loads(payload_str)
        if destructive:
            span_secs = conf.invite_span
            if 'span' in payload:
                span_secs = payload['span']
            await red.expire(f"usekey:{usekey}", datetime.timedelta(seconds=span_secs), lt=True)
    else:
        # JWT
        try:
            payload = jwt.decode(token, conf.jwt_secret, algorithms=["HS256"])
        except ValueError:
            raise HTTPException(status_code=401)

    if need_invite and 'can_invite' not in payload:
        raise HTTPException(status_code=403)
    
    return payload


async def grant_token(sub: str, exp_secs: float, span_secs: float | None = None, can_invite: bool = False, one_use: bool = False) -> str:
    payload = {
        'sub': sub,
    }
    if span_secs:
        payload['span'] = span_secs
    if can_invite:
        payload['can_invite'] = True
    if one_use:
        payload['one_use'] = True
        usekey = secrets.token_urlsafe(16)
        await red.set(f"usekey:{usekey}", json.dumps(payload), ex=datetime.timedelta(seconds=exp_secs))
        return f"u{usekey}"
    else:
        payload['exp'] = datetime.datetime.utcnow() + datetime.timedelta(seconds=exp_secs)
        return jwt.encode(payload, key=conf.jwt_secret, algorithm="HS256")


@app.get("/check")
async def check(bearer: HTTPAuthorizationCredentials = Depends(HTTPBearer())) -> dict[str, Any]:
    return await authorize(bearer.credentials, destructive=False)


@app.post("/login")
async def login(google_token: str) -> str:
    email = await authorize_google(google_token)
    return await grant_token(email, conf.google_expiration, can_invite=True)


@app.post("/invite")
async def invite(bearer: HTTPAuthorizationCredentials = Depends(HTTPBearer()), expiration_secs: float | None = None, span_secs: float | None = None) -> str:
    user = await authorize(bearer.credentials, need_invite=True)
    if expiration_secs is None:
        expiration_secs = conf.invite_expiration
    if span_secs is None:
        span_secs = conf.invite_span
    return await grant_token(f"invitado de {user['sub']}", exp_secs=expiration_secs, span_secs=span_secs, one_use=True)


@app.get("/open")
async def open(door: int, bearer: HTTPAuthorizationCredentials = Depends(HTTPBearer())):
    await authorize(bearer.credentials)

    if door not in [0, 1]:
        raise HTTPException(status_code=400, detail="Invalid door id")

    async with manager.lock:
        manager.open[door] = True
        await manager.lock.wait_for(lambda: not manager.open[door])

    return {
        "message": f"Door {door} opened"
    }


@app.websocket("/status")
async def status(token: str, ws: WebSocket):
    await authorize(token)
    await ws.accept()
    last_heartbeat = time.monotonic()
    heartbeat_interval = 1.5

    async with manager.lock:
        manager.need_status += 1
        log.info("opening status connection (%s active)", manager.need_status)
        manager.lock.notify_all()
    try:
        cur_status = None
        while True:
            async with manager.lock:
                if not manager.running:
                    break
                new_status = tuple(manager.status) if manager.status else None
                log.debug("new_status: %s", new_status)
                if new_status == cur_status:
                    async with asyncio.timeout(heartbeat_interval):
                        await manager.lock.wait()
            if new_status != cur_status or time.monotonic() - last_heartbeat >= heartbeat_interval:
                # Send this new status
                await authorize(token)
                cur_status = new_status
                try:
                    await ws.send_json(new_status)
                except ConnectionClosedOK:
                    break
                last_heartbeat = time.monotonic()
    finally:
        async with manager.lock:
            manager.need_status -= 1
            log.info("closing status connection (%s active)", manager.need_status)
        await ws.close()
