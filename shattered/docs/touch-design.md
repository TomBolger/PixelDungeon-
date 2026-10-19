# Touch for the Shattered watch app (design)

## Why touch does nothing today
The Shattered watch app (`shattered/watch/src/c/main.c`) never subscribes to the touch service, so the watch drops every touch. The phone has no touch path either: `WatchService` only reads `KEY_HELLO`, `KEY_BUTTON` and `KEY_VERSION`.

## 1. How Pebble PD does it (watch/src/c/main.c)
- **API.** SDK 4.33.1 `pebble.h` (emery) provides:
  - `touch_service_is_enabled()`, `touch_service_subscribe(handler, ctx)` and `touch_service_unsubscribe()`.
  - Events are `TouchEvent { TouchEventType type; bool non_navigational; int16_t x, y; }`, with types `TouchEvent_Touchdown`, `TouchEvent_PositionUpdate` and `TouchEvent_Liftoff`. Coordinates are screen pixels (200×228).
  - PD subscribes in `init` only if `touch_service_is_enabled()`, and unsubscribes on exit. It does not call `app_touch_navigation_enable`, so the system never turns touches into button presses.
  - Gesture recognizers also exist in the SDK, but PD does not use them.
- **Tap or drag** (lines 990–1037):
  - `TAP_SLOP 40`: a gesture moving less than 40 px (Manhattan distance, measured as the maximum over the gesture) is a tap. The owner reported that taps were lost at 14 px.
  - `PAN_START 24`: on the map (below the status bar and above the toolbar), a 24 px slide starts panning, and a pan never taps.
  - **Long press:** held at least 500 ms at liftoff (`s_long_press`). In the backpack, a 500 ms hold timer fires while the finger is still down, to pick an item up.
  - **Lone liftoff:** a quick tap can arrive as a liftoff with no touchdown; it counts as a tap at the liftoff point.
  - Every touch calls `woke()`, which resets the idle-animation timer.
  - **Text scroll:** in item and info text, a vertical slide of 8 px or more scrolls the text.
- **Tap regions** (`on_tap`, lines 928–988):
  - **Windows and front screens:** `ui_input(-1 tap / -2 hold, x, y)`, the window's own hit test.
  - **Portrait**, plus a finger's width around it: recentre the camera; when it isn't showing, open the character sheet.
  - **Menu button:** opens the menu.
  - **Health strip:** ignored.
  - **Toolbar row:**
    - Wait (long press: rest).
    - Search.
    - Info (inspect mode, then tap a cell).
    - Bag (long press: catalogue).
    - Quickslots 1 and 2 (long press: choose an item).
  - **Tags:** loot (pick up underfoot), resume, the attack/target tag, and the zoom and next-target tags.
  - **Map:** tap a cell to walk or act (`game_tap_cell`). While aiming, tap a cell to throw; a toolbar tap cancels.
  - **Dead screen:** the bag still opens; otherwise go to the menu after 1 s.

## 2. Design for Shattered
Principle: the watch only decides tap vs drag and where the finger was, and shows a pan right away. The phone feeds Shattered real pointer events, so Shattered's own hit tests handle everything: `CellSelector` (walk, act, aim), `Button` and `PointerArea` (toolbar, tags, quickslots with real long press, windows, outside-the-window-to-close), and `ScrollPane` (lists).

### 2.1 Protocol (version 3 → 4)
**Watch → phone:** new key `KEY_TOUCH = 6`, a 12-byte byte array, one gesture event per message.
```
[0] kind   1 TAP   2 PAN (map drag delta)   3 DOWN   4 MOVE   5 UP   (3-5: a drag on the interface)
[1] seq    u8, +1 per message (the phone echoes it back, see C_INPUT)
[2] cam    the watch camera id the touch-down hit (0xFF: nothing); Link camera ids = phone cameras
[3] flags  bit0 LONG (held >= 500 ms)
[4..5] x   s16 watch px (PAN: dx since the last PAN)
[6..7] y   s16 watch px (PAN: dy)
[8..11] stamp u32: the phone-clock ms of the frame the watch was SHOWING at touch-down
```
Traffic:
- A tap is one message. A drag is coalesced: while the outbox is busy, PAN deltas add up and MOVE keeps only the latest point. That gives at most one message per radio round trip, never more than 10 per second.

**Phone → watch:**
- `C_INPUT = 12`, `[seq u8]`, written into the stream in the frame where the phone applied that touch.
- `C_CAM` mode byte: bit `0x80` marks `Camera.main`, the map camera. The low 7 bits are the old mode.

Bump `Link.PROTOCOL` and `main.c PROTOCOL` to 4.

### 2.2 Watch (C, `shattered/watch/src/c`)
**Hit test.** Add to `draw.c` / `scene.c` a helper that reuses the draw transform:
```c
// scene.h
extern int8_t g_map_cam;                 // the map camera (C_CAM mode bit 0x80), -1 none
extern int32_t g_pan_x, g_pan_y;         // local pan preview on the map camera (cam units x256)
int scene_hit(int x, int y, uint32_t now);   // camera id of the topmost picture under (x,y), -1 none

// scene.c, read_cam(): the map flag
  int m = u8(); k->mode = m & 0x7F; if (m & 0x80) g_map_cam = id;

// draw.c: screen rect of an upright picture, same maths as draw_obj
static bool obj_rect(Obj *o, uint32_t now, int32_t *L, int32_t *T, int32_t *R, int32_t *B) {
  if (o->cam >= MAX_CAM || !g_cam[o->cam].used || o->kind == K_TILES) return false;
  if (obj_dyn(o, 2, now) < 30 && o->kind != K_TEXT) return false;          // invisible
  s_cam = &g_cam[o->cam];
  int32_t X = obj_dyn(o, 0, now), Y = obj_dyn(o, 1, now), sx = obj_dyn(o, 3, now), sy = obj_dyn(o, 4, now);
  int32_t l = X + o->ox - o->ox * sx / 256, t = Y + o->oy - o->oy * sy / 256;
  *L = scx(l) >> 3; *T = scy(t) >> 3; *R = scx(l + o->w * sx / 256) >> 3; *B = scy(t + o->h * sy / 256) >> 3;
  return true;   // (text: w/h are its measured box; turned pictures: their upright box is close enough)
}
int scene_hit(int x, int y, uint32_t now) {
  int cam = -1;
  for (int id = g_head; id != NONE; id = g_obj[id]->next) {   // draw order: the last hit is on top
    Obj *o = g_obj[id]; int32_t L, T, R, B;
    if (o->cam == g_map_cam) { Cam *c = &g_cam[o->cam];      // the map: anywhere in its viewport
      if (x >= c->x >> 3 && y >= c->y >> 3 && x < (c->x + c->w) >> 3 && y < (c->y + c->h) >> 3) cam = o->cam; continue; }
    if (obj_rect(o, now, &L, &T, &R, &B) && x >= L && x < R && y >= T && y < B) cam = o->cam;
  }
  return cam;
}
```
**Pan preview.** In `draw_obj`, draw the map camera shifted by the preview offset:
```c
  static Cam s_tmp;
  if (o->cam == g_map_cam && (g_pan_x || g_pan_y)) { s_tmp = *s_cam; s_tmp.sx += g_pan_x; s_tmp.sy += g_pan_y; s_cam = &s_tmp; }
```
**Gestures** (`main.c`), using PD's thresholds:
```c
enum { KEY_TOUCH = 6 };
enum { T_TAP = 1, T_PAN, T_DOWN, T_MOVE, T_UP };
#define TAP_SLOP 32      // UI drag starts (PD: 40 everywhere; scroll lists need a bit less)
#define PAN_START 24     // map drag starts (PD)
#define LONG_MS 500

static int16_t s_tx, s_ty, s_lx, s_ly, s_moved; static bool s_down, s_drag, s_map; static int s_tcam;
static uint32_t s_tms, s_tstamp; static uint8_t s_seq;
static uint32_t s_cur_stamp;   // set in pump() next to s_cur_at: stamp_at(...) of the frame being played

// pending preview deltas, removed when the phone's C_INPUT(seq) for them is played
typedef struct { uint8_t seq; int16_t dx, dy; } PanDelta;
static PanDelta s_pd[8]; static int s_npd;

// ---- outbox: small touch messages, coalesced while the radio is busy ----
typedef struct { uint8_t b[12]; } TMsg;
static TMsg s_tq[6]; static int s_ntq; static bool s_sending;
static void tq_send(void) {
  if (s_sending || !s_ntq) return;
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;           // busy: outbox_sent will call again
  dict_write_data(it, KEY_TOUCH, s_tq[0].b, 12);
  if (app_message_outbox_send() == APP_MSG_OK) { s_sending = true; memmove(s_tq, s_tq + 1, --s_ntq * sizeof(TMsg)); }
}
static void sent(DictionaryIterator *it, void *c) { s_sending = false; tq_send(); }
static void failed(DictionaryIterator *it, AppMessageResult r, void *c) { s_sending = false; app_timer_register(100, (AppTimerCallback)tq_send, NULL); }
static void put16(uint8_t *p, int v) { p[0] = v; p[1] = v >> 8; }
static void touch_msg(int kind, int x, int y, bool lng) {
  TMsg *last = s_ntq ? &s_tq[s_ntq - 1] : NULL;
  if (last && last->b[0] == kind && (kind == T_PAN || kind == T_MOVE)) {      // coalesce
    if (kind == T_PAN) { x += (int16_t)(last->b[4] | last->b[5] << 8); y += (int16_t)(last->b[6] | last->b[7] << 8); }
  } else { if (s_ntq == 6) return; last = &s_tq[s_ntq++]; last->b[1] = ++s_seq; }
  uint8_t *b = last->b; b[0] = kind; b[2] = s_tcam < 0 ? 0xFF : s_tcam; b[3] = lng;
  put16(b + 4, x); put16(b + 6, y);
  b[8] = s_tstamp; b[9] = s_tstamp >> 8; b[10] = s_tstamp >> 16; b[11] = s_tstamp >> 24;
  if (kind == T_PAN) {                       // remember for the preview (by the seq that will carry it)
    if (s_npd && s_pd[s_npd - 1].seq == b[1]) { s_pd[s_npd - 1].dx = x; s_pd[s_npd - 1].dy = y; }
    else if (s_npd < 8) s_pd[s_npd++] = (PanDelta){ b[1], x, y };
  }
  tq_send();
}
static void pan_preview(void) {               // sum of deltas the phone hasn't applied yet, in camera units x256
  int dx = 0, dy = 0; for (int i = 0; i < s_npd; i++) { dx += s_pd[i].dx; dy += s_pd[i].dy; }
  Cam *c = g_map_cam >= 0 ? &g_cam[g_map_cam] : NULL;
  g_pan_x = c && c->zoom ? -(int32_t)dx * 8 * 65536 / c->zoom : 0;   // finger right -> view moves right -> scroll left
  g_pan_y = c && c->zoom ? -(int32_t)dy * 8 * 65536 / c->zoom : 0;
  kick();
}
// scene.c calls this when it plays C_INPUT(seq): those deltas are now in the camera's own scroll
void touch_acked(uint8_t seq) {
  int k = 0; for (int i = 0; i < s_npd; i++) if ((int8_t)(s_pd[i].seq - seq) > 0) s_pd[k++] = s_pd[i];
  s_npd = k; pan_preview();
}

static void touch(const TouchEvent *e, void *ctx) {
  uint32_t now = now_ms(); s_input_at = now;
  switch (e->type) {
    case TouchEvent_Touchdown:
      s_down = true; s_drag = false; s_tx = s_lx = e->x; s_ty = s_ly = e->y; s_moved = 0; s_tms = now;
      s_tstamp = s_cur_stamp + (now - s_cur_at);                         // what was on screen, in phone time
      s_tcam = scene_hit(e->x, e->y, now); s_map = s_tcam >= 0 && s_tcam == g_map_cam;
      break;
    case TouchEvent_PositionUpdate: {
      if (!s_down) break;
      int m = abs(e->x - s_tx) + abs(e->y - s_ty); if (m > s_moved) s_moved = m;
      if (!s_drag && s_moved >= (s_map ? PAN_START : TAP_SLOP)) {
        s_drag = true;
        if (!s_map) touch_msg(T_DOWN, s_tx, s_ty, false);              // the interface: a real drag from the start point
      }
      if (s_drag) {
        if (s_map) { touch_msg(T_PAN, e->x - s_lx, e->y - s_ly, false); pan_preview(); }
        else touch_msg(T_MOVE, e->x, e->y, false);
        s_lx = e->x; s_ly = e->y;
      }
      break;
    }
    case TouchEvent_Liftoff:
      if (s_drag) { if (!s_map) touch_msg(T_UP, e->x, e->y, false); }
      else if (!s_down) { s_tstamp = s_cur_stamp + (now - s_cur_at); s_tcam = scene_hit(e->x, e->y, now); touch_msg(T_TAP, e->x, e->y, false); }   // lone liftoff (PD)
      else touch_msg(T_TAP, s_tx, s_ty, now - s_tms >= LONG_MS);
      s_down = s_drag = false;
      break;
  }
}
// init():  app_message_register_outbox_sent(sent, NULL); app_message_register_outbox_failed(failed, NULL);
//          if (touch_service_is_enabled()) touch_service_subscribe(touch, NULL);
// main():  touch_service_unsubscribe();
// scene_apply(): case C_INPUT: touch_acked(u8()); break;
```
Notes:
- Raise the outbox in `app_message_open(in, 64)` to at least 64. A 12-byte array with its key uses about 22 bytes, so 64 is enough.
- `send_u32` (buttons) must use the same queue and busy flag, or a button can be lost while a touch is in flight. Put buttons in `s_tq` as a 12-byte record with `kind = 0`, or send them through the same busy check.
- On hello or reset, clear `s_npd`, `s_ntq`, `g_pan_*` and set `g_map_cam = -1`.

### 2.3 Phone (Java)
**`WatchService.onMessageReceived`:**
```java
static final int KEY_TOUCH = 6;
PebbleDictionaryItem t = data.get(KEY_TOUCH);
if (t instanceof PebbleDictionaryItem.Bytes) s.touch(((PebbleDictionaryItem.Bytes) t).getValue());
```
`Server.java` (bridge frame `'T'` + 12 bytes) calls the same `Touches.add(bytes)`. Bridge (`tools/bridge.py`), in `on_msg`:
```python
KEY_TOUCH = 6
if KEY_TOUCH in data: to_server(b'T', bytes(data[KEY_TOUCH]))
```
The libpebble2 `AppMessageService` already decodes byte arrays as `bytes`, so no other bridge change is needed.

**`common/src/pebxel/Touches.java`** (new; queued like buttons and run on the game thread before `app.frame()`). It injects real pointer events through `Game.inputHandler`, the same calls libGDX makes for a finger, so `PointerEvent` processing in `Game.step` routes them through Shattered's normal hit tests:
```java
public class Touches {
	static final int ID = 0;                       // pointer id
	private final ArrayDeque<byte[]> in = new ArrayDeque<>();
	private float upAt = -1; private int upX, upY; // a press waiting to be let go
	private float dragDx, dragDy;                  // remap offset for the current interface drag
	public synchronized void add(byte[] b) { in.add(b); }

	/** Call on the game thread before each app.frame(). */
	public void frame(Link link, Controls controls, float now) {
		if (upAt >= 0 && now >= upAt) { Game.inputHandler.touchUp(upX, upY, ID, 0); upAt = -1; }
		byte[] b;
		while ((b = poll()) != null) {
			int kind = b[0], seq = b[1] & 255, cam = b[2] & 255; boolean lng = (b[3] & 1) != 0;
			int x = (short) ((b[4] & 255) | b[5] << 8), y = (short) ((b[6] & 255) | b[7] << 8);
			int stamp = (b[8] & 255) | (b[9] & 255) << 8 | (b[10] & 255) << 16 | (b[11] & 255) << 24;
			controls.touched();                       // the button frame steps aside, as in PD
			switch (kind) {
				case 1: { // TAP
					int[] p = link.remap(cam, stamp, x * 2 + 1, y * 2 + 1);
					if (p == null) break;                 // the screen it was aimed at is gone: drop it
					if (upAt >= 0) Game.inputHandler.touchUp(upX, upY, ID, 0);
					Game.inputHandler.touchDown(p[0], p[1], ID, 0);
					upX = p[0]; upY = p[1];
					upAt = now + (lng ? Button.longClick + 0.1f : 0.001f);   // let go next frame, or after a long press
					break;
				}
				case 2: { // PAN the map
					if (Ui.topWindow() == null && Game.scene() instanceof GameScene) {
						Camera m = Camera.main;
						m.shift(new PointF(-x * 2 / m.zoom, -y * 2 / m.zoom));   // what CellSelector.onDrag does
					}
					break;
				}
				case 3: { // DOWN: an interface drag starts
					Window w = Ui.topWindow();
					if (w != null && Controls.tall(w)) break;  // tall windows: MOVE scrolls them instead
					int[] p = link.remap(cam, stamp, x * 2 + 1, y * 2 + 1);
					if (p == null) break;
					dragDx = p[0] - (x * 2 + 1); dragDy = p[1] - (y * 2 + 1);
					Game.inputHandler.touchDown(p[0], p[1], ID, 0); break;
				}
				case 4: { // MOVE
					Window w = Ui.topWindow();
					if (w != null && Controls.tall(w)) { Controls.scrollTo(w, y * 2 + 1); break; }   // follow the finger
					Game.inputHandler.touchDragged((int) (x * 2 + 1 + dragDx), (int) (y * 2 + 1 + dragDy), ID); break;
				}
				case 5: // UP
					Game.inputHandler.touchUp((int) (x * 2 + 1 + dragDx), (int) (y * 2 + 1 + dragDy), ID, 0); break;
			}
			link.ack(seq);                            // C_INPUT(seq) goes out in this frame's changes
		}
	}
	private synchronized byte[] poll() { return in.poll(); }
}
```
- `touchDown` and `touchUp` queue `PointerEvent`s, which `Game.step` processes through `inputHandler.processAllEvents`. Let go one frame after the press, so `PointerArea` and `Button` see DOWN, then UP, then `onClick`.
- For a long press, keep holding for `Button.longClick` (0.5 s) plus 0.1 s, so `Button.update` fires `onLongClick`. That covers quickslot assign, wait/rest and the bag catalogue.
- `Controls.touched()`: `onToolbar = false;` and hide the target frame until the next button press.
- `Controls.scrollTo(w, y)`: remember the finger's y at DOWN, then `scroll(w, y - lastY)` (make `tall`/`scroll` package-visible).

**`Link` additions:**
```java
/** Camera states per frame, to place a tap on what the watch was showing. */
private static final class Snap { int ms; Object top; Map<Integer, float[]> cams = new HashMap<>(); }
private final ArrayDeque<Snap> hist = new ArrayDeque<>();
// in frame(dt), after sync: record (cheap: a handful of cameras)
	Snap s = new Snap(); s.ms = Math.round(now * 1000); s.top = Ui.topWindow() != null ? Ui.topWindow() : Game.scene();
	for (Map.Entry<Camera, Integer> e : camIds.entrySet()) { Camera c = e.getKey();
		s.cams.put(e.getValue(), new float[]{c.x, c.y, c.zoom, c.scroll.x, c.scroll.y}); }
	hist.add(s); while (!hist.isEmpty() && s.ms - hist.peek().ms > 3000) hist.poll();

/** A point the watch touched at its frame `stamp`, in phone pixels now (null: what it was aimed at is gone). */
public int[] remap(int camId, int stamp, int px, int py) {
	if (camId == 0xFF) return new int[]{px, py};
	Camera now = null; for (Map.Entry<Camera, Integer> e : camIds.entrySet()) if (e.getValue() == camId) now = e.getKey();
	if (now == null) return null;
	Snap then = null; for (Snap s : hist) if (s.ms <= stamp) then = s;
	if (then == null) return new int[]{px, py};
	Object top = Ui.topWindow() != null ? Ui.topWindow() : Game.scene();
	if (then.top != top) return null;                               // a window opened/closed since: don't guess
	float[] c = then.cams.get(camId); if (c == null) return null;
	float wx = (px - c[0]) / c[2] + c[3], wy = (py - c[1]) / c[2] + c[4];   // Camera.screenToCamera, then
	return new int[]{ Math.round((wx - now.scroll.x) * now.zoom + now.x), Math.round((wy - now.scroll.y) * now.zoom + now.y) };   // cameraToScreen, now
}
public void ack(int seq) { cmd(C_INPUT); u8(seq); }
// cameras(): mode byte |= 0x80 when c == Camera.main
```

**`WatchSession` / `Server`:** add `touches.frame(link, controls, gameTime)` next to `controls.button(...)`. `gameTime` is the sum of `Game.elapsed`, the same clock Link stamps with.

### 2.4 The play-out delay: decision
The watch shows the scene about 0.2–0.4 s behind the phone. When does that matter?
- **Standing still** (most taps in a turn-based game): the cameras haven't moved, so the remap changes nothing.
- **While the hero walks**, the map camera follows him at about 10 tiles per second. A tap aimed at what the watch showed would land 2–4 tiles off on the phone's current camera. This is the case that matters.
- **Tall windows** scrolled by buttons or drags: the window camera y also lags on the watch.

Decision: send the stamp of the frame on screen at touch-down, and the camera the finger hit. The phone keeps 3 s of per-frame camera states and converts the point:
1. From screen to world, using the camera as it was at that stamp.
2. From world back to screen, using the camera as it is now.

It then injects the point. Shattered's own hit test then lands on the world spot the player touched, even though the camera has moved since.

If the top window or scene changed between the stamp and now (a window opened or closed), the tap is dropped rather than sent to whatever is now under the finger. The player sees the new screen a moment later and taps again. This guards against, for example, a "Yes" landing on the map after a dialog has already closed.

No rewind of game state is needed. Monsters may have moved a tile, but the player taps cells, not monsters. `CellSelector` resolves sprites near the tapped cell itself.

**Pan feel:** without a preview, a drag would show up only after the round trip (about 0.5 s) and feel rubbery. So the watch moves the map camera locally at once by the unacknowledged deltas. The phone applies each PAN with `Camera.main.shift` (which also stops the camera following, as in Shattered) and echoes `C_INPUT(seq)` in that same frame. When the watch plays that frame, the camera's own scroll now includes the pan, and the same deltas are removed from the preview in the same frame, so nothing jumps. If the phone ignores a PAN (a window is open), the ack still arrives with no camera change, and the preview springs back.

## 3. Emulator testing
- **The bridge** carries the new key as shown above (`'T'` frames to `Server`). No libpebble2 change is needed.
- **There is no `pebble emu-touch`.** `emu-tap` is an accelerometer tap, and libpebble2's QEMU protocol has no touch packet.
- **The emulator does model touch.** `qemu-pebble` 10.1.5-pebble14 contains a `pebble-touch` device (`hw/misc/pebble_touch.c`) fed by QEMU's pointer input. Two ways to drive it:
  - **QEMU monitor (HMP).** pebble-tool starts QEMU with `-monitor tcp::PORT`, and the port is stored in `/tmp/pb-emulator.json` under `[platform][version]['qemu']['monitor']`. The HMP commands `mouse_move X Y` and `mouse_button 1` / `mouse_button 0` exist in the binary. Whether `mouse_move` is absolute for this device needs checking: send `mouse_move 100 114`, `mouse_button 1`, `mouse_button 0` and watch the app log for a touch. A test helper:
    ```python
    # tools/touch.py  usage: touch.py tap X Y | drag X1 Y1 X2 Y2
    import json, socket, sys, tempfile, os, time
    info = json.load(open(os.path.join(tempfile.gettempdir(), 'pb-emulator.json')))['emery']
    port = list(info.values())[0]['qemu']['monitor']
    s = socket.create_connection(('127.0.0.1', port)); time.sleep(0.2); s.recv(4096)
    def hmp(c): s.sendall((c + '\n').encode()); time.sleep(0.05)
    a = sys.argv[1:]
    if a[0] == 'tap': hmp(f'mouse_move {a[1]} {a[2]}'); hmp('mouse_button 1'); time.sleep(0.08); hmp('mouse_button 0')
    else:
        x1, y1, x2, y2 = map(int, a[1:5]); hmp(f'mouse_move {x1} {y1}'); hmp('mouse_button 1')
        for i in range(1, 11): hmp(f'mouse_move {x1 + (x2 - x1) * i // 10} {y1 + (y2 - y1) * i // 10}'); time.sleep(0.03)
        hmp('mouse_button 0')
    ```
  - **VNC.** `pebble install --emulator emery --vnc` starts QEMU with `-vnc :1` (port 5901). VNC sends absolute pointer events in screen pixels: `pip install vncdotool`, then `vncdo -s localhost::5901 move 100 114 click 1`, or `mousedown 1` / `drag 100 160` / `mouseup 1`. Use this if HMP turns out to be relative.
- **Phone side without the emulator:** a `'T'` frame sent straight to `Server` on port 9180 (a 12-byte touch) exercises `Touches` and `Link.remap` with `tour.sh`/`buttons.sh`-style pictures. `Buttons.java` could accept `t:X,Y` in its script to call `touches.add(...)` directly.

## 4. Check list once built
- **Taps:**
  - A tap on a floor cell walks there; a tap on a monster attacks; a tap on the hero acts on the stairs or item underfoot.
  - A tap on a toolbar button works: wait, search, bag, a quickslot.
  - Long press: a quickslot opens its item picker; wait rests.
  - Taps on window buttons work; a tap outside a window closes it; list rows in the journal and bag work.
- **Drags:**
  - A map drag pans at once on the watch and settles without a jump. A tap after a pan still hits the right cell.
  - A drag in a tall window scrolls it; a drag in a ScrollPane list scrolls it.
- **Delay:**
  - A tap on a cell while the hero is walking lands on the tapped cell.
  - A tap on a dialog that has just closed is dropped.
