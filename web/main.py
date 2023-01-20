
from datetime import datetime
import traceback
from typing import Callable
from fastapi import FastAPI, Header, HTTPException, Query
from fastapi.responses import HTMLResponse
import esp32
import asyncio

app = FastAPI()


@app.get("/")
async def root():
    return HTMLResponse("""
<html>
    <head>
    </head>
    <body>
        <button onclick="abrir()">Abrir puerta chica</button>
        <br/>

        Puerta chica:
        <output id="chica">?</output>
        <br/>

        Porton:
        <output id="grande">?</output>
        <br/>

        <script>
            var password = prompt("Contraseña?")

            function setIndicator(elem, status) {
                elem.innerText = (status ? "Abierta" : "Cerrada")
                elem.style.color = (status ? "red" : "green")
            }

            function checkStatus() {
                fetch("/status", {
                    headers: {
                        'Authorization': password,
                    },
                }).then(res => res.json())
                .then(res => {
                    setIndicator(document.getElementById("grande"), res.doors[0])
                    setIndicator(document.getElementById("chica"), res.doors[1])
                }).catch(err => console.error(err))
                .finally(() => {
                    setTimeout(checkStatus, 1000)
                })
            }
            checkStatus()

            function abrir() {
                fetch("/open?door=1", {
                    method: 'POST',
                    headers: {
                        'Authorization': password,
                    }
                }).catch(err => console.error(err))
            }
        </script>
    </body>
</html>
""")


correct_password = "triple83"


incorrect = 0


async def authorize(password: str):
    if password != correct_password:
        incorrect += 1
        await asyncio.sleep(2**incorrect)
        raise HTTPException(status_code=401, detail="Incorrect password")
    else:
        incorrect = 0


conn_cache = None
last_request = {}


def do_request(kind: str, func: Callable[[esp32.Esp32], None]):
    global last_request
    global conn_cache

    if (datetime.now() - last_request.setdefault(kind, datetime.fromtimestamp(0))).total_seconds() < 0.5:
        raise HTTPException(status_code=429)
    for tries in range(3):
        try:
            if conn_cache is None:
                conn_cache = esp32.Esp32()
            ret = func(conn_cache)
            last_request[kind] = datetime.now()
            break
        except Exception:
            traceback.print_exc()
            conn_cache = None
    return ret


@app.get("/status")
async def status(authorization: str = Header()):
    authorize(authorization)

    levels = do_request('status', lambda conn: conn.readstatus())
    return {
        'doors': list(map(lambda x: x >= esp32.conf['threshold'], levels))
    }


@app.post("/open")
async def opendoor(door: int = Query(...), authorization: str = Header()):
    authorize(authorization)

    if door not in [0, 1]:
        raise HTTPException(status_code=400)

    do_request('open', lambda conn: conn.opendoor(door, 750))
    return {"message": "Door opened"}
