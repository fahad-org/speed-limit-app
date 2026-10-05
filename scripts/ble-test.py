"""Talks to the device over Bluetooth from the PC, to check the protocol in esp32/speed_limit/ble.h (needs: pip install bleak).
  python scripts/ble-test.py [pin] [command-json ...]
Examples:
  python scripts/ble-test.py                          # connect, log in with 1234, print the state a few times
  python scripts/ble-test.py 1234 '{"c":"set","off":5}'
"""
import asyncio, json, sys
from bleak import BleakScanner, BleakClient

SVC = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'; RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
pin = sys.argv[1] if len(sys.argv) > 1 else '1234'
cmds = sys.argv[2:]
buf = ''

def on_tx(_, data):
    global buf
    buf += data.decode(errors='replace')
    while '\n' in buf:
        line, buf = buf.split('\n', 1)
        print('<-', line[:230] + (' ...' if len(line) > 230 else ''))

async def main():
    print('scanning...')
    dev = await BleakScanner.find_device_by_filter(lambda d, ad: SVC in [u.lower() for u in (ad.service_uuids or [])] or d.name == 'SpeedLimit', timeout=15)
    if not dev: print('device not found'); return
    print('found', dev.name, dev.address)
    async with BleakClient(dev) as c:
        print('connected, mtu', c.mtu_size)
        await c.start_notify(TX, on_tx)
        async def send(o):
            s = json.dumps(o, separators=(',', ':')) + '\n'
            print('->', s.strip())
            data = s.encode()
            step = max(20, c.mtu_size - 3)
            for i in range(0, len(data), step): await c.write_gatt_char(RX, data[i:i + step], response=False)
        await send({'c': 'get'})                      # before login: must be refused
        await asyncio.sleep(1)
        await send({'c': 'auth', 'pin': pin})
        await asyncio.sleep(2.5)
        for x in cmds:
            await send(json.loads(x)); await asyncio.sleep(3)
        await asyncio.sleep(2)

asyncio.run(main())
