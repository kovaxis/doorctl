#!/usr/bin/env python3

if __name__ == "__main__":
    import sys
    print("run using uvicorn instead")
    sys.exit()

from contextlib import asynccontextmanager
import datetime
import os
from pathlib import Path
from fastapi import Depends, FastAPI, HTTPException, WebSocket
import jwt
from websockets.exceptions import ConnectionClosedOK
from fastapi.responses import HTMLResponse
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from pydantic import BaseModel
from google.oauth2 import id_token
from google.auth.transport import requests
from app.manager import Esp32Manager
import logging

log = logging.getLogger("main")



class WebConf(BaseModel):
    email_whitelist: set[str]
    jwt_secret: bytes
    google_expiration: float
    invite_expiration: float


with open("./conf-web.json", 'r') as file:
    conf = WebConf.model_validate_json(file.read())
index_html = Path(__file__).with_name("index.html").read_text()


manager = None


@asynccontextmanager
async def lifespan(app: FastAPI):
    global manager
    logging.basicConfig(
        level=os.environ.get('LOGLEVEL', 'INFO').upper(),
    )
    manager = Esp32Manager()
    try:
        yield
    finally:
        await manager.close()


app = FastAPI(lifespan=lifespan)

CLIENT_ID = "200671129298-58096itq8l7g8uskluhqbte5asl0dbv2.apps.googleusercontent.com"


@app.get("/")
async def root():
    return HTMLResponse(index_html)


def authorize_google(google_token: str) -> str:
    try:
        userdata = id_token.verify_oauth2_token(google_token, requests.Request(), CLIENT_ID)
        email = userdata['email']
    except ValueError:
        raise HTTPException(status_code=401)
    if email not in conf.email_whitelist:
        raise HTTPException(status_code=403, detail="Not in whitelist")
    return email


def authorize(token: str, need_invite: bool = False) -> str:
    try:
        payload = jwt.decode(token, conf.jwt_secret, algorithms=["HS256"])
    except ValueError:
        raise HTTPException(status_code=401)
    
    if need_invite and 'can_invite' not in payload:
        raise HTTPException(status_code=403)
    
    return payload['sub']


def grant_token(sub: str, exp_secs: float, can_invite: bool = False) -> str:
    payload = {
        'sub': sub,
        'exp': datetime.datetime.utcnow() + datetime.timedelta(seconds=exp_secs),
    }
    if can_invite:
        payload['can_invite'] = True
    return jwt.encode(payload, key=conf.jwt_secret, algorithm="HS256")


@app.get("/check")
async def check(bearer: HTTPAuthorizationCredentials = Depends(HTTPBearer())) -> str:
    return authorize(bearer.credentials)


@app.post("/login")
async def login(google_token: str) -> str:
    email = authorize_google(google_token)
    return grant_token(email, conf.google_expiration, can_invite=True)


@app.post("/invite")
async def invite(bearer: HTTPAuthorizationCredentials = Depends(HTTPBearer())) -> str:
    subject = authorize(bearer.credentials, need_invite=True)
    return grant_token(f"invitado de {subject}", exp_secs=conf.invite_expiration)


@app.get("/open")
async def open(door: int, bearer: HTTPAuthorizationCredentials = Depends(HTTPBearer())):
    authorize(bearer.credentials)

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
    authorize(token)
    await ws.accept()

    async with manager.lock:
        manager.need_status += 1
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
                    await manager.lock.wait()
            if new_status != cur_status:
                # Send this new status
                cur_status = new_status
                try:
                    await ws.send_json(new_status)
                except ConnectionClosedOK:
                    break
    finally:
        async with manager.lock:
            manager.need_status -= 1
