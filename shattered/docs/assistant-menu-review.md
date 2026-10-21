# Assistant and menu refinements

Based on `main` at `333c3cc` (there is no remote Shattered branch). The honest solver already calls the
same `common/src/pebxel/Assist.java` as the watch. `BotItems`, `BotQuests`, and `BotAlchemy` overrides are
cheat-mode coverage tools, not stronger policies to copy into player suggestions.

## Changes

- Assistant: avoid raw mystery meat (cooked meat still works); do not rest in locked boss fights; rest
  before a boss using the visible floor-lock state instead of inspecting unseen bosses. Compare unknown
  gear using its displayed, base strength requirement, without revealing a hidden upgrade. Give the Mage's
  staff its first three upgrades before returning to the existing armor-first policy. These follow
  `strategy-reference.md` sections 1.1, 2.1 and 2.6 and the corresponding game source.
- Touch: interface drags now send movement and release, clear lost releases on the next press, and cancel
  when their window/page changes. Tall-window gestures retain their original scroll owner. Pending long
  taps are also cancelled if their page changes. A delayed tap stamped on a former menu/hero/catalogue
  page cannot hit that window's new page.
- Menus: tapping the active tab preserves its content. Settings changes rebuild only the affected tile
  (changing assist also refreshes item-assist availability). Other buttons and focus remain stable.
- Watch renderer: adjacent text lines share a framebuffer release/capture interval. A later raster object,
  including a particle emitter, recaptures it. Drawing order is preserved; no new framebuffer allocation.

No protocol or save-format change. Both phone/server and watch sources changed: rebuild the phone app
to receive the assistant and menu fixes, and the watch app for the rendering change.

## Validation

- `shattered/build-server.sh`: successful Java 11-target build with JDK 17.
- `pebble build` in `shattered/watch`: successful Emery and Gabbro builds; reported free heap budgets
  76,906 and 74,382 bytes respectively.
- `pebxel.RefinementTest`: passes on Emery and Gabbro. Covers safe food, locked-floor rest, hidden
  upgrade/strength information, Mage staff priority, stale page stamps, active-tab identity, drag
  movement/release/cancellation, pending long-tap cancellation, and stable settings button/listener identity.
- `pebxel.MenuTest 60`: repeated browsing and closing of menu, bag, hero, catalogue, journal and item
  windows leaves touch listeners at their starting count. Added a 24-toggle music settings measurement:

  | Same test with baseline/current common code | Baseline | Changed |
  |---|---:|---:|
  | Bytes for 24 music toggles | 3,214 | 1,794 |
  | Messages | 30 | 30 |
  | Touch listeners before/after | 60/60 | 60/60 |

  This is **44% fewer bytes for this interaction**, not a claim that all menus are 44% faster. Ordinary
  page browsing remains about 24 KB for the 60-switch test. The byte total is measured at the Link, before
  radio framing/retries.
- `pebxel.RadioTest menu bag`: default 3,000 B/s, 100 ms RTT; and 1,000 B/s, 250 ms RTT, 10% NACKs:
  both finish with zero messages the watch model could not apply and no full resends. The slow run
  actually retried three NACKs. Network latency remains significant.
- Host test of the real C renderer:

  ```sh
  cd shattered
  cc -O1 -Itools/feel/stub -Iwatch/src/c tools/feel/draw_test.c -o /tmp/draw-test
  /tmp/draw-test
  ```

  Passes ownership/order checks for adjacent text, mixed raster/text, and emitters after text.
- Honest bot A/B: 120 matching seeds (1000–1119, all six classes, `SIMSEED=42`, quick C1 JIT).
  Mean maximum depth: **2.442 baseline / 2.367 changed**, no wins in either. This sample does **not** demonstrate stronger
  survival. Both versions retain the same GreatCrab loop at Huntress seed 1099; there are no new reported
  loops, action failures, timeouts or crashes. Keep the behavioral fixes distinct from a claim of improved win rate. A broader proposed
  upgrade-allocation rewrite was discarded after a worse result (2.308).

To run Java checks, use the existing server/game/lib classpath from `spd/core/src/main/assets`, as for
`MenuTest`. Physical-watch frame times and real Bluetooth behavior still need device testing; the C host
test verifies ownership and ordering, not actual font rendering or watch CPU time.
