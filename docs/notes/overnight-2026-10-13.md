# Overnight report: Oct 12–13, 2026 (build 0.50; solver bot 0.55/0.56)

## Headlines
- **First win with no head start** (respawns on): round 16, game 20, a Warrior with scale armour +6. It died 31 times (3 to DM-300, 6 to the King, about 14 on depth 24). The pattern held all night: heavy armour carrying every upgrade.
- **Full head start with no respawns: 3 wins in 18** (games 1, 3 and 20), all deathless. Most of the rest died on Yog's floor.
- **No head start and no respawns (the true game): still 0 wins.** Mean depth 4.7 → 5.0, mostly deaths to crabs, Goo and starvation on depths 3–5. This is now the main target.
- **Watch 0.50:** the suggester has new positioning tactics, the test kit's hands can zap, throw, read and drink, and one game bug is fixed.

## Night run (rounds n3a–n3e on the PC)
| arm | games | wins | lockouts | mean max depth | past Goo / Tengu / DM-300 / King / Yog | by class |
|---|---|---|---|---|---|---|
| 0.55 bot, respawns on | 120 | 0 | 2 | 16.6 | 120 / 111 / 57 / 14 / 2 | W18.4 M16.9 R16.5 H14.5 |
| 0.55 + armour gets every upgrade | 119 | 1 | 3 | 16.4 | 119 / 110 / 61 / 9 / 1 | W18.3 M17.0 R16.2 H14.2 |
| 0.56 bot, respawns on | 96 | 1 | 2 | 17.3 | 96 / 92 / 61 / 11 / 1 | W17.8 M17.6 R18.0 H15.7 |
| 0.56b bot (last fixes), respawns on | 96 | 1 | 3 | 17.6 | 96 / 93 / 64 / 14 / 3 | W18.8 M17.8 R18.1 H15.7 |
| 0.55 true game (no respawns) | 120 | 0 | 0 | 4.7 | 23 / 3 / 0 / 0 / 0 | W5.7 M5.1 R4.1 H3.8 |
| 0.56 true game | 120 | 0 | 0 | 5.0 | 29 / 5 / 0 / 0 / 0 | W5.5 M5.5 R5.1 H3.7 |
| 0.56 true game, old hunger rule | 118 | 0 | 0 | 4.9 | 28 / 3 / 0 / 0 / 0 | W5.5 M5.4 R5.1 H3.8 |
| watch suggester + new tactics | 72 | 0 | 2 | 15.2 | 70 / 65 / 37 / 0 / 0 | W16.4 M15.6 R14.8 H13.8 |
| watch suggester, old | 66 | 0 | 5 | 15.0 | 63 / 61 / 33 / 1 / 0 | W15.6 M15.0 R15.0 H14.6 |

All runs: no head start, daytime only, same seeds per row (game N is the same dungeon in every row).
- "Lockouts" counts games that ended stuck.
- "Respawns on" means each death is logged and the hero carries on, up to 40 times.
- The watch suggester rows are the bot playing only by the middle button with its own hands.
- Head-to-head on the same seeds:
  - 0.56 vs 0.55 (respawns): +0.5 ± 0.4 floors.
  - 0.56b vs 0.55 (respawns): +0.8 ± 0.45 floors.
  - 0.56b true game: 5.1, the same as 0.56.
  - 0.56 vs 0.55 (true game): +0.3 ± 0.2.
  - New vs old suggester: +0.0 ± 0.4, with fewer lockouts (2 vs 5).
  - New vs old hunger rule (true game): −0.1 ± 0.1. No difference.

**Wins with no head start, respawns on: 6 tonight out of about 600 games** (rounds 16, 17 ×3, n3a, n3d).
- All were Warriors (4) or Rogues (2).
- They used 28–40 respawns.
- Each finished in its class armour from the King's armour kit.

**Per arrival** (0.56, respawns on): Tengu costs 6.3 lives, DM-300 9.1, the King 6.8. Upgrade scrolls collected: 69%.

**Small head start (HS1: +30 HP, +1 STR, +3 skills), respawns on, 0.56b: 21 wins in 72 (29%)**, up from 2 in 12 at the start of the night. Mean depth 23.4; 29 of 72 got past Yog.

**Full head start, no respawns:** 3 of 18 won, all deathless. 11 of the 18 died on Yog's floor.


## What changed in the solver bot tonight
(all behind env switches for A/B)
- **Hunger:**
  - Keep exploring while hungry until actually starving.
  - Pick up items in sight before going down.
  - Owner's rule kept: starving and below 30% HP, leave. 10% tested no better.
- **Unknown armour:** a possible curse no longer scares it off. An Upgrade scroll lifts the curse and adds a level.
- **Trapped prizes:** an Upgrade scroll, Strength potion, Enchantment scroll, wand, ring or gear lying on a trap: throw junk onto the trap first, then take both (owner's tip).
- **Storage rooms:**
  - Fixed: barricades were never burned. The throw check didn't allow for a throw stopping one cell short.
  - It now burns them; the night's runs logged 11–105 burn attempts per 100 games.
  - An unknown potion found on that floor is tried as the fire, since the room's painter always leaves a Liquid Flame there.
- **Pit rooms:**
  - Fixed: the bot never jumped in.
  - Puzzle rooms are now done before jumping.
  - The remains are opened at 50% HP.
- **Pool rooms:** drink-test unknown potions at the door until Invisibility turns up, then swim in for the prize (owner).
- **Transmutation well:** a Strength potion goes in and comes back as Might; a known Enchantment scroll comes back as Upgrade. It no longer wastes the well on a worse weapon.
- **Huntress:**
  - Boomerang every turn while an enemy approaches, and point-blank when that beats her dagger.
  - Upgrade scrolls can go into the boomerang, never the dagger.
  - Fights are judged with her boomerang counted.
  - Same-seed test: mean depth 13.5 → 15.8.
- **Altar:** creatures are lured from up to 24 steps (slow ones, while 3+ are alive, from depth 4). The bot stands on the cell opposite where they come in, so they cross the altar. Wipe Out is saved for depths 22–24. Still 0 completions: see Next.
- **Magic Mapping and Mind Vision:** saved for depths 22–24, then the bot rushes the stairs if level 20+ (owner).
- **Bosses:**
  - Tengu: no dart throws (each is a step not taken). A curare dart when adjacent. Tengu lab: 2/12 → 5/12.
  - DM-300: curare and slowness. "Never let it walk" tested worse (more gas deaths), so it's opt-in.
  - King: stand on the pedestal he wants next, and stay there.
  - Yog: fight the rotting fist on dry ground, don't wash ooze in its pool, eat and rest on the stairs before stepping off.
  - The look-ahead no longer throws gas potions within 3 cells. A Tengu death was the bot gassing itself.
  - Eat before any boss door.
- **Lockouts fixed:**
  - Stair ping-pong while starving.
  - A puzzle walk that never arrived.
  - An item on the stairs.
  - Every target failing in turn (a blacklist that kept being reset).

## Watch 0.50 (`pixel-dungeon-0.50.pbw`, `-testkit.pbw`)
- **Feet (both builds): where to stand.**
  - The door trick against a creature coming from afar.
  - Out of an evil eye's or scorpio's line.
  - DM-300: hold a cell with no dead trap on it or beside it.
  - The rotting fist on dry ground; the burning fist from the water beside it.
  - Items on the stairs are no longer suggested (a tap there takes the stairs).
- **Hands (test kit):**
  - A full wand, or any charge when below half HP, at the nearest creature hunting you in line (never Lightning or Avalanche point blank).
  - The Huntress's boomerang every turn; darts from 3+ cells.
  - Known Strength potions; known Upgrade scrolls (the selector asks what for).
  - Unknown scrolls read when nothing is about.
- **Game fix:** a thief that falls into a chasm with your item now drops it on the floor at once. It used to reappear only after you left and came back.
- **Measured:** the bot playing only by the middle button: 15.2 vs 15.0 mean depth, and 2 vs 5 lockouts. Neutral but tidier, so it's in.

## Next
1. **The true game (no respawns):** depths 3–5 (crabs, Goo, starving).
   - Heroes reach depth 3 under-levelled; one Huntress was level 2 with 20 HP, and a crab killed her.
   - Resting to 90% before going down on depths 1–4 didn't help (5.6 vs 5.9, 24 games).
   - Next: level gates before the first floors, crab tactics (they're too fast for the door trick), and food.
2. **Lockouts:** still 2–3 per 100 games with respawns on.
   - Two were on the dark Halls floors: the exit was behind a secret door, and searching in the dark misses walls the hero can't see. Use a torch, or the saved Magic Mapping; the bot now reads it after 20 fruitless searches.
   - One was a ping-pong in a very long game.
3. **About 30% of Upgrade scrolls and Strength potions still missed.** The biggest buckets are open-but-unexplored floor and secret doors.
4. **DM-300 and Tengu:** still about 7 lives each per game with no head start.
5. **Yog** for deathless wins, and evil eyes on depths 22–24.
6. **Altar:** sacrifices now happen ("your sacrifice is worthy"), but the fire needs 5+5×depth. On a cleared floor that's too many kills, so none completed. Next: wait at the altar for respawning creatures once it's half fed.
