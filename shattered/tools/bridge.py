"""Relays between the screenless game (server/Server.java, TCP) and a Pebble or the Pebble emulator,
playing the part the Android app plays: AppMessages to the watch app, buttons back.
Run it with the pebble tool's Python:  .../pebble-tool/bin/python bridge.py [emulator-platform] [port]

The emulator's link is a local websocket: instant and nearly unlimited. A real Pebble Time 2 on Android, through
the Pebble app, is not: by default this bridge makes the emulator's link behave like the real one (the Android
app, WatchSession + WatchService, sends the same way: one message, wait for its answer, then the next).
Settings (environment):
  RADIO=0          the emulator's own link, as fast as it goes (no model)
  RTT_MS=100       one round trip (message out, answer back), the quiet-radio part; real: about 50-150 ms
  JITTER_MS=40     each half of a round trip varies by up to half this, either way
  RATE=3000        bytes a second the radio carries (each message also takes its size / RATE); real: a few KB/s
  NACK_PCT=0      messages refused (the watch's answer is a nack; the phone app sends it again). The watch app
                   answers each message when its inbox handler returns and the phone sends the next only then, so
                   the watch refuses only a message bigger than its inbox (modelled below) or while the app closes
  DROP_PCT=0       messages lost (no answer at all: the phone hears a failure after TIMEOUT_MS). Under the Pebble
                   app (libpebble3) the link is reliable: BLE retransmits at the link layer and PPoGATT on top
                   acknowledges and resends its packets, so a message is not lost while connected; when one is
                   (a dropped connection), nothing comes back for 10 s
  TIMEOUT_MS=10000 how long a lost message takes to count as failed: libpebble3's AppMessage timeout
                   (APPMESSAGE_TIMEOUT, and PebbleKit 2's WATCH_SENDING_TIMEOUT) and the phone's Pacer.WATCHDOG
  INBOX=0          the watch's inbox in bytes (0: the size the watch app says in its hello); bigger messages are
                   refused, as the real watch does
  LAG=ms           extra time before the game hears a press or touch (added to half a round trip)
Buttons and touches from the watch take half a round trip to reach the game, and the watch's outbox stays busy
for a whole one (as on the real watch, it sends the next only when the phone has answered).
Every 5 s it prints what went: bytes and messages a second, refusals, losses, mean time per message.
"""
import json, os, queue, random, socket, struct, sys, threading, time, uuid
from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.websocket import WebsocketTransport
from libpebble2.services.appmessage import AppMessageService, ByteArray
from libpebble2.protocol.appmessage import AppMessage, AppMessagePush, AppMessageACK, AppMessageTuple

APP = uuid.UUID('84d52e02-b98a-4d9c-b021-a9e3c442d0ca')
KEY_DATA, KEY_HELLO, KEY_BUTTON = 1, 2, 3

def env(k, d): return float(os.environ.get(k, d))
RADIO = os.environ.get('RADIO', '1') != '0'
RTT, JIT, RATE = env('RTT_MS', 100) / 1000, env('JITTER_MS', 40) / 1000, env('RATE', 3000)
NACK, DROP, TIMEOUT = env('NACK_PCT', 0) / 100, env('DROP_PCT', 0) / 100, env('TIMEOUT_MS', 10000) / 1000
INBOX, LAG = int(env('INBOX', 0)), env('LAG', 0) / 1000

def half(): return max(0.005, RTT / 2 + (random.random() - 0.5) * JIT) if RADIO else 0

def emulator_port(platform):
    info = json.load(open('/tmp/pb-emulator.json'))[platform]
    return list(info.values())[-1]['pypkjs']['port']

class SlowAppMessages(AppMessageService):
    """Messages from the watch: handed on after half a round trip, answered after a whole one."""
    def _handle_message(self, packet):
        if RADIO and isinstance(packet.data, AppMessagePush):
            # (as the base class, but its answer held back)
            result = {}
            for t in packet.data.dictionary:
                if t.type == AppMessageTuple.Type.ByteArray: result[t.key] = bytearray(t.data)
                elif t.type == AppMessageTuple.Type.CString: result[t.key] = t.data.split(b'\x00')[0].decode('utf-8', errors='replace')
                else: result[t.key], = struct.unpack(self._type_mapping[(t.type, t.length)], t.data)
            self._broadcast_event("appmessage", packet.transaction_id, packet.data.uuid, result)
            tid = packet.transaction_id
            threading.Timer(2 * half(), lambda: self._pebble.send_packet(AppMessage(transaction_id=tid, data=AppMessageACK()))).start()
            return
        super()._handle_message(packet)

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
    am = SlowAppMessages(pebble)
    st = {'msgs': 0, 'bytes': 0, 'nacks': 0, 'lost': 0, 'busy': 0.0, 't0': time.time()}
    inbox = [INBOX]

    def later(fn, delay):
        if delay > 0: threading.Timer(delay, fn).start()
        else: fn()
    def on_msg(tid, app, data):
        if app != APP: return
        up = half() + LAG
        if KEY_HELLO in data:
            if not INBOX: inbox[0] = data[KEY_HELLO]
            later(lambda: to_server(b'H', struct.pack('>HB', data[KEY_HELLO], data.get(5, 1))), up)
            print('watch hello', data[KEY_HELLO], 'version', data.get(5, 1))
        if KEY_BUTTON in data: b = data[KEY_BUTTON] & 255; later(lambda: to_server(b'B', bytes([b])), up); print('button', data[KEY_BUTTON])
        if 6 in data: t = bytes(data[6]); later(lambda: to_server(b'T', t), up); print('touch', t.hex())
    am.register_handler('appmessage', on_msg)

    # ---- to the watch: one message at a time, like the phone app (the server waits for each answer) ----
    answers = queue.Queue()
    am.register_handler('ack', lambda tid, app: answers.put((tid, True)))
    am.register_handler('nack', lambda tid, app: answers.put((tid, False)))
    jobs = queue.Queue()

    def radio():
        while True:
            m = jobs.get()
            t0 = time.time()
            ok = deliver(m)
            st['busy'] += time.time() - t0
            to_server(b'A' if ok else b'N')

    def deliver(m):
        """One AppMessage the real way; True once the watch has it."""
        st['msgs'] += 1; st['bytes'] += len(m)
        if not RADIO:
            return send_and_wait(m, 10)
        if inbox[0] and len(m) + 8 > inbox[0]:   # (dictionary: count byte, 7 bytes of tuple header)
            print('message of %d bytes is bigger than the watch inbox (%d): refused' % (len(m), inbox[0]))
            time.sleep(2 * half()); st['nacks'] += 1; return False
        r = random.random()
        if r < DROP:
            print('message lost (no answer for %.1f s)' % TIMEOUT)
            time.sleep(TIMEOUT); st['lost'] += 1; return False
        time.sleep(half() + len(m) / RATE)        # out over the air
        if r < DROP + NACK:
            print('message refused (nack)'); time.sleep(half()); st['nacks'] += 1; return False
        ok = send_and_wait(m, TIMEOUT)          # the watch takes it (and its app works through it)
        time.sleep(half())                      # the answer back
        return ok

    def send_and_wait(m, timeout):
        while not answers.empty(): answers.get_nowait()
        tid = am.send_message(APP, {KEY_DATA: ByteArray(m)})
        end = time.time() + timeout
        while time.time() < end:
            try: got, ok = answers.get(timeout=end - time.time())
            except queue.Empty: break
            if got == tid:
                if not ok: print('the watch refused a message (nack)'); st['nacks'] += 1
                return ok
        print('the watch never answered'); st['lost'] += 1
        return False

    threading.Thread(target=radio, daemon=True).start()
    def report():
        while True:
            time.sleep(5)
            dt = time.time() - st['t0']
            if st['msgs']:
                print('radio: %.0f bytes/s, %.1f messages/s, %.0f ms a message, %d nacks, %d lost' % (
                    st['bytes'] / dt, st['msgs'] / dt, st['busy'] * 1000 / st['msgs'], st['nacks'], st['lost']))
            st.update(msgs=0, bytes=0, busy=0.0, t0=time.time())
    threading.Thread(target=report, daemon=True).start()
    print('radio model:', 'off (emulator speed)' if not RADIO else
          'round trip %.0f ms, %.0f bytes/s, %.1f%% nack, %.1f%% lost' % (RTT * 1000, RATE, NACK * 100, DROP * 100))

    buf = b''
    while True:
        chunk = tcp.recv(65536)
        if not chunk: break
        buf += chunk
        while len(buf) >= 2:
            n = struct.unpack('>H', buf[:2])[0]
            if len(buf) < 2 + n: break
            frame, buf = buf[2:2 + n], buf[2 + n:]
            if frame[:1] == b'D': jobs.put(frame[1:])

if __name__ == '__main__':
    main()
