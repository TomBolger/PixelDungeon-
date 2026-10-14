# Changes made to the copied Pixel Dungeon 1.9.1 sources

`build.sh` copies `third_party/pixel-dungeon/src/com/...` plus `third_party/pd-classes/com/...` into `build/src`
and applies `headless.patch`, which holds every edit listed here.
Every edited line is marked `/*HEADLESS*/`, except the three cast fixes in item 1.
Read `headless.patch` to see them all.
None of the edits changes game rules.

## Edits to existing files

1. **Compile fix for javac 21 (no behaviour change).** These files are
   `items/Heap.java:373`, `actors/mobs/Mimic.java:67` and `levels/RegularLevel.java:682`.
   The code `(Collection<? extends Item>) bundle.getCollection(..)` no longer compiles
   ("incompatible types"). It is now `(Collection<? extends Item>)(Collection<?>) bundle.getCollection(..)`,
   and the same for `Room`.

2. **`utils/Random.java` (pd-classes): seedable RNG.** Every `Math.random()` is now
   `rnd()`, which reads a private static `java.util.Random`. `Math.random()` itself uses
   one shared `java.util.Random`, so the algorithm and distribution stay the same.
   `Random.seed(long)` was added so runs can be repeated (`--seed`).

3. **`scenes/GameScene.java`: static helpers work without a scene (`scene == null`).**
   - `add(Mob)`, `add(Mob,float)`: add the mob to the level and the actors as before,
     then attach a `HeadlessSprite` instead of `scene.addMobSprite`.
   - `add(Heap)`, `discard(Heap)`: attach a `HeadlessItemSprite`. Game code often calls
     `level.drop(..).sprite.drop()`, which needs a sprite.
   - `emitter()`: returns a new detached `Emitter` instead of `null`. The callers call
     `.burst()` / `.pour()` on it.
   - `ripple()` / `spellSprite()`: return fresh detached objects.
   - `add(EmoIcon)`, `effect`, `pickUp`, `show(Window)`, `flash`, `gameOver`, `bossSlain`,
     `handleCell`, `selectCell`, `ready`: return early. `selectItem`: returns `null`.

4. **`ui/AttackIndicator.java`.** `target()` and `updateState()` return early when the
   UI instance is `null`. They are UI only; `lastTarget` is still set.

5. **`scenes/PixelScene.java`.** `showBadge()` returns early when there is no UI
   camera or scene. Badges are still validated and recorded.

## New files (headless additions)

- `sprites/HeadlessSprite.java` is a `CharSprite` that does nothing visual.
  - `attack()`, `attack(cell, cb)`, `operate()` and `jump()` **queue** their completion
    callback (`ch.onAttackComplete()` and so on). The driver runs the queue with
    `HeadlessSprite.flush()` after `Actor.process()` returns. This copies the real loop,
    where `act()` returns false and the animation finishes on a later frame.
  - `move()` calls `ch.onMotionComplete()` straight away, as the original does. `isMoving`
    stays false.
  - `link()` still uses up one `Random.Int(LENGTH)`, as the original `turnTo` call does.
- `sprites/HeadlessItemSprite.java` is a heap sprite with no-op `drop`, `place` and `update`.

## Stubs (`stubs/`, not game code)

- `android.opengl.GLES20`: every call is a no-op. `glGen*` hands out increasing ids,
  and the `glGet*iv` status queries report success.
- `android.graphics.*`: `Bitmap` is a fake pixel array. `BitmapFactory` always returns a
  256x256 bitmap, so `TextureCache` and `TextureFilm` work. `RectF` is real.
  `Canvas`, `Paint` and the gradients do nothing.
- `android.util.SparseArray` is a full working version (sorted keys, like Android's).
  `FloatMath` and `opengl.Matrix.multiplyMM` are real maths.
- `android.app.Activity`, `content.Context` and `SharedPreferences` keep preferences in
  memory. File I/O (`openFileInput` / `openFileOutput`) throws `FileNotFoundException`.
  The game catches it, so save, bones and rankings files are skipped quietly.
- `android.media.*` (`SoundPool`, `MediaPlayer`), `android.view.*` and `android.os.*` are no-ops.
- `android.util.Log`: `Log.i` prints only with `--trace`, and this is where `GLog` text appears.
- `org.json.*` stores data in memory only. `JSONTokener` (parsing) is not supported, so
  loading saved games will not work.
