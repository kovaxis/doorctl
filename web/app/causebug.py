import asyncio
import logging
import sys
from app.esp32 import Esp32

log = logging.getLogger("causebug")

async def main():
    esp32 = Esp32()
    await esp32.connect()
    esp32.writer.write(b"1")
    try:
        print("disconnect from the internet now (waiting 100 seconds)", file=sys.stderr)
        await asyncio.sleep(100)
        log.info("closing...")
    finally:
        await esp32.close()

if __name__ == "__main__":
    logging.basicConfig()
    asyncio.run(main())
