import asyncio
import traceback
from typing import Awaitable, Callable, TypeVar
from app.esp32 import Esp32
import logging

log = logging.getLogger("manager")

T = TypeVar("T")

class Esp32Manager:
    running: bool
    open: list[bool]
    status: list[bool] | None
    need_status: int

    lock: asyncio.Condition
    esp32: Esp32
    task: Awaitable[None]

    def __init__(self):
        self.running = True
        self.open = [False, False]
        self.status = None
        self.need_status = 0

        self.lock = asyncio.Condition()
        self.esp32 = Esp32()
        self.task = asyncio.create_task(self.run())

    async def close(self):
        async with self.lock:
            self.running = False
            self.lock.notify_all()
        await self.task

    async def act_on_esp32(self, act: Callable[[Esp32], Awaitable[T]]) -> T | None:
        # Carry out the action
        try:
            return await act(self.esp32)
        except Exception:
            traceback.print_exc()
            log.warn("could not communicate with esp32")
            return None
    
    async def run(self):
        while True:
            # Check actions
            actions = []
            async with self.lock:
                if not self.running:
                    break
                log.debug("reading actions")
                for door, open in enumerate(self.open):
                    if open:
                        actions.append(("open", door))
                if self.need_status > 0:
                    actions.append(("read", None))

                # If there is nothing to do, wait until there is something to do
                if not actions:
                    log.debug("no actions, waiting for notification")
                    await self.lock.wait()
                    log.debug("woke up from slumber")
                log.debug("actions: %s", actions)
            
            # Carry out actions
            for action, arg in actions:
                if action == "open":
                    # Try to open door `door` for 750ms
                    door = arg
                    result = await self.act_on_esp32(lambda esp32: esp32.opendoor(door, 750))
                    if result:
                        async with self.lock:
                            self.open[door] = False
                            self.lock.notify_all()
                elif action == "read":
                    # Read the status of both doors, placing it in `self.status`
                    log.debug("manager reading status")
                    newstatus = await self.act_on_esp32(lambda esp32: esp32.readstatus())
                    log.debug("status is %s", newstatus)
                    async with self.lock:
                        self.status = newstatus
                        self.lock.notify_all()
            
            # Wait some time between actions
            if actions:
                await asyncio.sleep(0.25)
        
        # Close connection to ESP32
        if self.esp32 is not None:
            await self.esp32.close()
        print("manager closed")
