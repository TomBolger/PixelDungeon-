"""Relays between the screenless game (server/Server.java, TCP) and a Pebble or the Pebble emulator,
playing the part the Android app will play: AppMessages to the watch app, buttons back.
Run it with the pebble tool's Python:  .../pebble-tool/bin/python bridge.py [emulator-platform]"""
import json, socket, struct, sys, threading, time, uuid
from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.websocket import WebsocketTransport
from libpebble2.services.appmessage import AppMessageService, ByteArray

APP = uuid.UUID('84d52e02-b98a-4d9c-b021-a9e3c442d0ca')
KEY_DATA, KEY_HELLO, KEY_BUTTON = 1, 2, 3

def emulator_port(platform):
    info = json.load(open('/tmp/pb-emulator.json'))[platform]
    return list(info.values())[-1]['pypkjs']['port']

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    platform = args[0] if args else 'emery'
    tcp = socket.create_connection(('127.0.0.1', int(args[1]) if len(args) > 1 else 9180))
    tcp.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    lock = threading.Lock()
    def to_server(kind, data=b''):
        with lock: tcp.sendall(struct.pack('>H', len(data) + 1) + kind + data)

    pebble = PebbleConnection(WebsocketTransport('ws://localhost:%d/' % emulator_port(platform)))
    pebble.connect(); pebble.run_async()
    am = AppMessageService(pebble)
    stats = {'msgs': 0, 'bytes': 0, 't0': time.time()}

    def on_msg(tid, app, data):
        if app != APP: return
        if KEY_HELLO in data: to_server(b'H', struct.pack('>HB', data[KEY_HELLO], data.get(5, 1))); print('watch hello', data[KEY_HELLO], 'version', data.get(5, 1))
        if KEY_BUTTON in data: to_server(b'B', bytes([data[KEY_BUTTON] & 255])); print('button', data[KEY_BUTTON])
    am.register_handler('appmessage', on_msg)
    def ack(tid, app):
        if '-v' in sys.argv: print('ack', tid)
        to_server(b'A')
    am.register_handler('ack', ack)
    def nack(tid, app):
        print('watch refused a message (nack); retrying in 0.2 s'); time.sleep(0.2); resend()
    am.register_handler('nack', nack)

    last = [None]
    def resend():
        if last[0] is not None: am.send_message(APP, {KEY_DATA: ByteArray(last[0])})

    buf = b''
    while True:
        chunk = tcp.recv(65536)
        if not chunk: break
        buf += chunk
        while len(buf) >= 2:
            n = struct.unpack('>H', buf[:2])[0]
            if len(buf) < 2 + n: break
            frame, buf = buf[2:2 + n], buf[2 + n:]
            if frame[:1] == b'D':
                last[0] = frame[1:]
                tid = am.send_message(APP, {KEY_DATA: ByteArray(frame[1:])})
                if '-v' in sys.argv: print('sent', tid, len(frame) - 1)
                stats['msgs'] += 1; stats['bytes'] += len(frame) - 1
                if stats['msgs'] % 50 == 0:
                    dt = time.time() - stats['t0']
                    print('%d messages, %d bytes, %.0f bytes/s' % (stats['msgs'], stats['bytes'], stats['bytes'] / dt))

if __name__ == '__main__':
    main()
