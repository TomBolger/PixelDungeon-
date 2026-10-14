# javapd: original Pixel Dungeon 1.9.1 logic, headless

This runs watabou's Java game code unchanged in its rules: actors, buffs, blobs, combat
and AI. It needs only plain Java 21 and has no Android, no screen and no downloads.
It exists for "same scenario, compare outcomes" tests against the C port.

```
stubs/          android.*, org.json.*, javax.microedition.* stand-ins (see PATCHES.md)
headless.patch  the small edits that let the game run without a screen (PATCHES.md)
sim/            the driver: sim/Sim.java (scenarios) and sim/TestLevel.java (ASCII-art level)
build.sh        copies the upstream sources from third_party/, applies the patch, compiles into build/classes
```

## Build

```sh
cd tools/javapd
./build.sh                # needs the submodules: git submodule update --init
```

All 571 game and engine files compile, plus the stubs and the driver. A full build takes about 10 s.

## Run the scenarios

```sh
java -cp build/classes sim.Sim melee rat  2000      # warrior vs marsupial rat
java -cp build/classes sim.Sim melee crab 2000      # warrior vs sewer crab
java -cp build/classes sim.Sim burn door  2000      # hero set on fire while on a door
java -cp build/classes sim.Sim burn grass 2000      #   ... on grass (3x3 patch)
java -cp build/classes sim.Sim burn floor 2000      #   ... on plain floor
java -cp build/classes sim.Sim gas 2000             # ToxicGas 300 at room centre, hero 2 cells away
```

Options:

- `--seed N` makes runs repeatable.
- `--trace` prints the game log, status texts and every hit the hero takes. Use it with a small run count.
- `--class warrior|mage|rogue|huntress` picks the hero class.
- `--asleep` makes melee mobs start asleep. The default is awake (HUNTING).
- `--fire N` also seeds `Fire` of size N on the hero's cell in the burn scenarios.
- `--amount N` sets the ToxicGas seed size.
- Any mob class name works as well, e.g. `melee Gnoll`.

## Exact scenario setup (mirror this in the C port)

The common setup:

- `Dungeon.init()` runs with `StartScene.curClass` set.
- The hero is swapped for `Sim.SimHero`, a subclass that only logs damage. It is set up
  the same way: `live()` adds Regeneration and Hunger, then `HeroClass.initHero()`.
  The warrior starts with STR 11, HP 20, ShortSword and ClothArmor.
- `Dungeon.depth = 1`.
- The level is `TestLevel`, built with `level.create()`, which runs `buildFlagMaps`.
  The ASCII room is placed at offset (8,8) of the 32x32 map, and every other cell is wall.
- `Dungeon.switchLevel(level, heroCell)` runs `Actor.init()`, which makes the hero act
  first at time -MIN_VALUE, and adds the respawner. `nightMode` is then set to false.

The hero's policy works like a player tapping:

- `tap(cell)` is `if (hero.handle(cell)) hero.next();`
- `rest` is `hero.rest(false)`

The driver repeats `Actor.process()` and `HeadlessSprite.flush()` until the hero is
`ready` again. Each policy call counts as one "hero action".

- **melee**: closed 7x7 room. The hero and the mob are orthogonally adjacent in the
  middle row, and the mob starts in state `HUNTING`. Because `enemySeen` is still false,
  the mob's defense against the hero's first swing is 0, as in the original. The hero
  attacks while adjacent and rests otherwise. A fight ends when one side dies, with a
  cap of 500 actions.
- **burn**: the hero is in a 7x5 room. For `door` he stands on a door in the east wall,
  with a corridor outside. The driver then runs:
  - `level.press(heroPos, hero)`, as if he had stepped there. This turns a door into OPEN_DOOR.
  - `Buff.affect(hero, Burning.class).reignite(hero)`
  - with `--fire N`, also `GameScene.add(Blob.seed(pos, N, Fire.class))`

  The hero then rests until he has no Burning buff and no Fire is left.
  "Burn ticks" counts how many times Burning damaged him; each tick does
  `Random.Int(1,5)`, which is 1 to 4. While the hero stands on a flammable tile,
  Burning seeds Fire(4) there.
- **gas**: closed 7x7 room. `GameScene.add(Blob.seed(centre, 300, ToxicGas.class))`, and
  the hero stands 2 cells west of the centre and rests until the gas volume reaches 0.
  ToxicGas damage at depth 1 is `(HT+10)/40`, plus 1 with chance `((HT+10)%40)/40`. For
  HP 20 that is 0 or 1 per tick, so the hit count includes zero-damage ticks.

## Sample output (2000 runs, no seed)

```
MELEE warrior vs marsupial rat (mob awake/hunting), runs=2000
  hero win rate     : 100.00%  (wins 2000, deaths 0, timeouts 0)
  mean hero actions : 1.966 (all runs), 1.966 (wins)
  mean HP left (win): 18.380 / 20
  mean dmg taken    : 1.836 in 0.895 hits/run
MELEE warrior vs sewer crab (mob awake/hunting), runs=2000
  hero win rate     : 87.80%  (wins 1756, deaths 244, timeouts 0)
  mean hero actions : 4.604 (all runs), 4.424 (wins)
  mean HP left (win): 10.298 / 20
  mean dmg taken    : 11.337 in 3.291 hits/run
BURN hero(warrior) standing on door, initial Fire seed 0, resting; runs=2000
  mean burn ticks (Burning dmg events): 6.839
  mean Burning damage                 : 16.748
  mean hero turns until fire out      : 6.170   deaths: 716
  terrain under hero: before=6, afterwards {9=2000}   (OPEN_DOOR burns to EMBERS)
BURN ... floor: mean burn ticks 5.566, mean damage 13.755, deaths 298
BURN ... grass: mean burn ticks 6.786, mean damage 16.566, deaths 679
TOXIC GAS seed 300 at room centre, hero(warrior) 2 cells away resting; runs=2000
  mean ToxicGas damage : 5.301 in 7.000 hits
  mean turns until gas gone: 9.537   deaths: 0
```

Each scenario also prints a histogram, which is useful for distribution tests such as chi-square or KS.

## Notes and caveats

- "HP left" includes natural Regeneration during the fight. "dmg taken" is the exact
  sum of HP lost to each damage instance.
- `Actor.all` is a `HashSet`, so when two actors are due at the same time, the order
  follows identity hash order. That is the original game's behaviour. In practice
  `--seed` still gave identical results across runs.
- Animations finish "instantly but later": attack and operate callbacks are queued and
  run after `Actor.process()` returns, just as the real frame loop would run them.
- Hero death works. `Dungeon.fail`, `Rankings`, `Bones` and badges run, and their file
  writes fail quietly.
- Not supported: loading or saving games (no JSON parser), and anything that needs a
  live `GameScene`, such as windows or cell selection prompts. Those return early and do nothing.
- To write a new scenario, copy one of the methods in `sim/Sim.java` and give
  `TestLevel` your own ASCII art. Its legend is at the top of `TestLevel.java`.
