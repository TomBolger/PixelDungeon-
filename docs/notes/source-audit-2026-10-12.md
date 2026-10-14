# Source audit: game vs bot (2026-10-12)

Five reviewers compared the game source (C port + original Java) with deepbot.c. Fixes from these went into bot 0.52a, each behind an OLD* switch (OLDSEED, OLDQBUG, OLDGEARFIX, OLDDEW, OLDEMERG, OLDDUEL2, OLDMOBS, OLDHERO, OLDDUNG, OLDHUNT) for A/B testing (job aud1).


---

# Audit: consumables and magic items (deepbot.c vs Pebxel port)

Port = `pebxel-c/src`, bot = `bot3/deepbot.c`. In the line references, `use.c` is `ovl/use/use.c`, `wand.c` is `ovl/wand/wand.c`, `trap.c` is `ovl/trap/trap.c`, `gen.c` is `ovl/gen/gen.c`, and `hero_impl.h` and `game.c` are in `c/`.

## 1. Mechanics table

| Mechanic | Game (source) | Bot | Impact |
|---|---|---|---|
| Colour/rune pool | 12 colours, 12 runes shuffled per game (newgame.c:55-63). Known at start: warrior STR, mage Identify, rogue Mapping (newgame.c:65) | HANDLED (item_known everywhere) | LOW |
| Random potion odds | Heal 45, Exp 4, Toxic 15, Para 10, Flame 15, Lev 10, Mind 20, Purity 12, Invis 10, Frost 10. STR and Might are 0 here; STR comes only from the quota (gen.c:52, content.c:341) | PARTIAL: resample PW gives STR 8, Might 2 (2212) | LOW |
| Healing | Full HP; cures poison, cripple, bleed and weakness (use.c:68) | HANDLED: emergency below 33% (773) | — |
| Strength / Might / Experience | +1 STR (Might also +5 HT); a level (use.c:73-81) | HANDLED once known (617-619). Unknown ones are never tested after depth 2 (see gap 1) | HIGH |
| Drink-test safety | Flame goes out in water; gases leave through a door | HANDLED: test_cell / stash_for (577-612), but only `G.depth <= 2` (688-689) | HIGH |
| Mind Vision | 20 turns (use.c:83) | MISSING (only drunk while testing) | LOW |
| Purity | Drunk: 5 turns gas immunity. Thrown: clears gas within radius 2 (use.c:42,90) | MISSING | LOW-MED |
| Invisibility | 15 turns (use.c:89) | PARTIAL: in the ESC list (778) and pool puzzle (986), but fight() then strikes the adjacent mob and breaks it | MED |
| Levitation | 20 turns (use.c:82) | HANDLED (trap-room puzzle, 986) | — |
| Frost | Freezes radius 2 for 5-7 turns, puts out fire (use.c:26) | PARTIAL: thrown as "harmful" at d≥3 (761); never used on fire | LOW |
| Liquid Flame | Fire at the cell (use.c:25) | PARTIAL: thrown at d≥3 with no flammable-path check (761); never thrown at barricades (storage rooms) | MED |
| Toxic gas | 1000 volume that spreads 4 ways and fades 1 per turn; (HT+5+5d)/40 damage per turn (game.c:797,813) | PARTIAL: thrown at d≥3, and the cloud reaches the hero in about 3 turns | LOW-MED |
| Paralytic gas | Same cloud; 10 turns paralysis, refreshed every turn in it (game.c:804) | PARTIAL: thrown at d≥3, then fight() walks into the cloud to the frozen mob | MED |
| Throw-ID | Only harmful, Frost and Purity identify on shatter; the rest are wasted and stay unknown (use.c:55) | PARTIAL: lookahead may throw unknown potions (2172) | LOW |
| Random scroll odds | Id 30, Tele 10, RC 15, Rech 10, Map 15, Chal 12, Terror 8, Lull 8, Psi 4, Mirror 6, Upg 0 (quota 3 per 5 floors), Ench 1 (gen.c:58) | OK (SW, 2214) | — |
| Identify | Item picker (use.c:252) | PARTIAL: worn gear first (515-516), which identifies itself by use anyway | MED |
| Mapping | Reveals the map and secrets (use.c:118) | HANDLED when known (read on arrival, 2438). Unknown ones are read mid-floor (685) | MED |
| Recharging | Every wand to full (use.c:130) | MISSING (never read on purpose) | LOW-MED |
| Remove Curse | Uncurses everything, removes weakness (use.c:140) | HANDLED (628; have_rc drives gear trials, 638) | — |
| Teleport scroll | Random cell (use.c:147) | HANDLED (ESC, 778) | — |
| Challenge | Whole floor hunts you (use.c:162) | Read only unknown, at ≥90% HP | LOW |
| Terror | 10 turns; a hit ends it after the first turn (Mob.java:312, Terror.recover) | PARTIAL: fight() hits the adjacent fleeing mob | MED |
| Lullaby | FOV mobs fall asleep (use.c:186) | PARTIAL: emergency() still counts sleeping adjacent mobs (775) and spends the next escape item | MED |
| Psionic Blast | 1..2/3 HT to every mob in view, blinds them; hero blind 3-6 turns (use.c:198) | MISSING from ESC and kit | LOW-MED |
| Mirror Image | 3 allied copies (use.c:206) | PARTIAL: only below 33% with a foe adjacent | MED |
| Wipe Out | Kills every non-boss (use.c:377) | HANDLED (777, 859) | — |
| Unknown-scroll timing | — | Read whenever "safe" (685), which wastes Mapping (12.6%) and combat scrolls (about 30%) | MED |
| Wand charges | max 2+lvl (Magic Missile 3+lvl), cap 9 (items.c:44); +1 per 40 turns per wand (game.c:935-940) | HANDLED: "full wand isn't recharging" (845) | — |
| Wand ID by use | Kind known on the first zap (hero_impl.h:149); level and charges only after 40 zaps (166) | HANDLED (unknown wands zapped, 749, 856). Fairness: reads `ench` without IF_CHARGE_KNOWN (751) | LOW |
| Attack wands | Slow, Firebolt, Poison, Lightning, Amok, Disintegration, Avalanche, MM (wand.c) | HANDLED (341). Adjacent-zap self-harm not modelled (bug B3) | MED |
| Wand of Teleport | Sends the target to a random cell (wand.c:67) | PARTIAL: only at a non-adjacent foe below 1/3 HP (752) | MED |
| Blink / Flock / Reach / Regrowth | Escape jump; sheep wall for (p+3) turns; swap with or pull items from afar; grass (wand.c) | MISSING | LOW-MED |
| Rings | Acc 1.4^L (game.c:178), Eva 1.2^L (185), Haste 1.1^L (214), Mending 1.2^L (951), Satiety (636), Power +L wand power (1449), Elements (146), Shadows, Detection, Herbalism, Thorns, Haggler. Random rings: 70% +1/+2, 30% cursed −1/−2 (gen.c:75); identified after 200 turns worn (game.c:927) | HANDLED: wears unknown rings (666). PARTIAL: no ranking by kind; swaps only for a higher known level (673) | LOW-MED |
| Seed knowledge | Seeds always show their name, but `set_known` is never called for seeds (every call site is listed by `grep set_known`) | BUG: every seed check requires item_known (1052, 1056, 761) | MED-HIGH |
| Plant activation | Planting or throwing never triggers the plant underneath; only stepping onto it does (game.c:1148, Level.java:600) | BUG: Sungrass (701) and Earthroot (1052) "under me" do nothing | MED |
| Plants | Firebloom fire; Icecap freezes radius 1; Sorrowmoss poison; Dreamweed confusion; Sungrass HT/10 per 5 turns standing still; Earthroot absorbs damage up to HT; Fadeleaf teleports; Rotberry toxic gas + roots (trap.c:104-132) | PARTIAL (see bugs); the Goo Icecap-on-doorway throw (1358) is correct | — |
| Alchemy | ≥3 seeds make a potion. Its kind comes from one seed, picked weighted by count; 1/count chance of a random potion. Sungrass→Healing, Rotberry→STR, Fadeleaf→Mind... (trap.c:193-211) | PARTIAL: cooks any 3 seeds once per floor (534, 1913); no Sungrass ×3 → Healing | LOW-MED |
| Dew / vial | A drop heals when you are hurt and only fills the vial at full HP (hero_impl.h:73-80). The vial heals PCT[dew]% (use.c:356). It auto-revives at 10 drops (game.c:505) | BUG: drinks at <50% HP when safe with ≥5 drops (708); emergency ≥3 (774) | MED |
| Honeypot | Golden bee ally (trap.c:177; ally, game.c:58) | HANDLED (1441) | — |
| Bombs | 3×3; 1+d..10+2d damage, 2 turns paralysis, breaks flammable cells (trap.c:157) | HANDLED (d≥3; reserved for Goo, 742) | — |
| Missiles | Curare: paralysis 3; incendiary: burning; javelin: cripple 10; boomerang returns; adjacent throws at half accuracy; warrior missiles at +3 encumbrance (game.c:172,1036; hero_impl.h:229) | HANDLED (ranged_try, kit, goo_open, boss_reserve). The warrior's penalty is not modelled | LOW |
| Food | Ration 260, pasty 360, others 100. Starving (360): no regeneration, 30% chance of 1 damage per 10 turns (game.c:633). Mystery meat: 1 in 5 each of burn, roots, poison, slow (use.c:323) | HANDLED (1776-1787: eats at starving, meat in water) | — |
| Ankh | Revive | HANDLED (2730; want_buy) | — |
| Shop | One guaranteed Healing among 4 potions, every unknown potion priced 200g at d6 (content.c:82-83, hero_impl.h:33) | FAIRNESS BUG: want_buy tests the true kind (1513) | LOW |

## 2. Top gaps

**1. Unknown potions are never tested after depth 2 (HIGH).** Drink-testing is gated by `G.depth <= 2` (688-689). After that, unknown potions are only drunk as an emergency gamble or in the Goo pump.
- Healing is about 32% of random potions. With about 2.5 potions found on floors 1-2, roughly 38% of games reach depth 3 with Healing still unknown.
- The mage, rogue and huntress also miss Strength in about 17% of games: the first STR potion spawns on d1-2 only 83% of the time (quota 2 of 4 floors). Every later STR potion then sits unknown in the bag, and upg_plan's STR planning counts on it.
- **Rule:** in use_items_when_safe, drop the depth gate. Test when HP is at least 90%, `enemy_count()==0` and the floor is clear (`!floor_left()`), keeping the test_cell/stash_for discipline (water while Flame is unknown, a door while the gases are unknown). Once Toxic, Paralytic and Flame are all known, test anywhere with no water needed.
- Test the colour you hold the most of first.
- Risk: toxic damage grows with depth, about 3 per turn at d10, so 6-9 HP at full health. Acceptable.

**2. Seed tactics are dead code, and plants under the hero never fire (MED-HIGH).**
- `item_known(IK_SEED_*)` is always false: no `set_known` is ever called for seeds. So seed_tactic's Earthroot/Icecap/Sorrowmoss/Dreamweed/Fadeleaf branches (1052-1060) and the Firebloom throw (761) never run. Only the blind "unknown seed" gamble below 40% HP runs (1061), and it plants Sungrass or Earthroot in front of the enemy, which heals or does nothing.
- Fix: treat `item_cat(k)==CAT_SEED` as known everywhere.
- Plants trigger only when something steps onto them (game.c:1148). Planting Sungrass under yourself (701) gives no healing.
- **Rule for Sungrass:** plant, step to a free neighbour, step back, then `game_rest` until HP==HT (Sungrass gives HT/10 every 5 turns, while you stand still).
- **Rule for Earthroot:** the same (plant, step off, step back on), done one turn before contact, e.g. while the foe is 2 cells away.
- The trap-seed "plant then step back" pattern (1059, step_back) is correct once the known-check is fixed.

**3. Escape items are wasted right after use (MED-HIGH).**
- emergency() counts every non-passive adjacent mob (775), sleeping and fleeing ones included. So after Lullaby, Terror or Invisibility it spends the next escape item on the following turn.
- fight() then taps the adjacent mob (1429): that breaks invisibility, ends Terror after a turn (Terror.recover), and wakes a lulled mob.
- **Rule:**
  - In emergency(), count only `ST_HUNTING` adjacent mobs.
  - Add `s_escaped_until = now + 10 turns` after an ESC item.
  - While it is active and HP is below 60%, fight() must skip mobs that are not hunting. Instead, walk away: prep_retreat's out-of-sight cell search, or the stairs up.
  - Also skip adjacent mobs while `h->b.invis` (not just piranhas, 1304).

**4. Wand of Teleport/Blink as an escape (MED).** Wands are 10% Teleport and 11% Blink.
- Teleport is only fired at a non-adjacent foe below 1/3 HP (752). Zapping it at the adjacent foe is the best escape in the game: it is reusable and never fails on a normal floor.
- **Rule:** in emergency() before the unknown-potion gamble, zap a known Teleport wand with `ench>0` at the adjacent hunting mob (the biggest foe_dpt first).
- Blink: zap it at a cell 4+ away from the foes (power+4 range), then rest out of sight.

**5. Read unknown scrolls on arrival, not mid-floor (MED).**
- Reading at 685 whenever "safe" wastes Mapping (12.6% of unknowns). Mapping saves exploration and also reveals secret doors, which feed evidence_hidden and the searches.
- It also wastes Teleport, Terror, Lullaby, Psionic and Mirror Image (about 30%).
- **Rule:**
  - Hold unknown scrolls (unless the bag is full or a cursed item is worn). Read them during the first safe decision on a new non-boss floor (`s_floor_steps < 20`, no enemy in FOV).
  - Never on floor 1 for the rogue.
  - Mid-floor reading is still fine when a known-cursed item is worn (Remove Curse odds).

**6. Use known combat scrolls when the fight sum says "losing", not at 33% (MED).**
- kit_fight (839-863) uses wands, darts, potions and *unknown* scrolls, but never known Mirror Image, Psionic Blast, Lullaby or Terror. Those only fire in emergency() below 33% HP.
- **Rule:** in kit_fight, when `lose && s_lose_cost >= HP*0.6`:
  - read known Mirror Image (3 decoys), or
  - read Psionic Blast when 2+ foes are in view (up to 2/3 HT each), or
  - read Lullaby or Terror, then retreat per gap 3.

**7. Dew vial policy (MED).**
- A full vial (10 drops) revives you automatically at death with +HT (game.c:505). Drinking it while safe (708) throws that away when resting costs only time.
- **Rule:** drop the drink at 708 (unless starving).
- In emergency(), drink only when `G.dew < 10`. At 10, the auto-revive triggers anyway, and it lets you use your remaining HP first.

**8. Scroll of Identify targeting (MED).**
- pick_for(PICK_IDENTIFY) takes worn gear first (515-516), which identifies itself by use: weapon 20 hits, armour 10, ring 200 turns.
- **Better order:**
  1. the unknown potion colour held most often, or any unknown potion while Healing or STR is unknown;
  2. unknown wands;
  3. bag armour or weapons the gear model is weighing;
  4. worn gear.

**9. Recharging, Purity, Liquid Flame on barricades (LOW-MED).**
- Read known Recharging when the missing charges summed over all wands are ≥3 and a fight is likely, or at a boss-floor arrival.
- Purity:
  - drink it when stuck in paralytic gas next to a hunter;
  - throw it at your own cell when a gas cloud covers you and the foe is not in it.
- Liquid Flame: throw at a known barricade into an RT_STORAGE room (STRATEGY rule), standing on non-flammable ground at d≥3.

**10. Gas potions thrown too close (MED).**
- After throwing Paralytic at d=3 (761, 852), fight() waits 3 turns and then walks into the cloud (1479-1482). The hero is paralyzed beside the mob, with the paralysis refreshed every turn.
- **Rule:** after a gas throw, set `s_gas_avoid`. In fight(), if the target stands in gas (`gas_at`), do not path into it: use ranged_try, otherwise step away or rest outside the cloud.
- Require d≥4 for gas potions. Liquid Flame needs `fire_safe` along the line.

## 3. Bugs (the bot's model is wrong)

- **B1, seeds:** `item_known(seed)` is never true (only potion, scroll, wand and ring kinds are ever `set_known`). Seed tactics and the Firebloom throw are dead. See gap 2.
- **B2, planting under yourself:** it never activates the plant. Sungrass at 701 and Earthroot at 1052/1400 are wasted (Level.plant does not activate the plant; plant_ovl runs only in hero_press).
- **B3, adjacent wand zaps:** kit_fight's comment "it never misses, beside you too" (843) is wrong for three wands.
  - Lightning arcs to any unhit neighbour, the hero included, at ½ to full damage, doubled when the hero stands in water (wand.c:44-54).
  - Avalanche hits every cell within 1+p/3 of the target, the hero included, for 2..6+ damage with a 1/(2+d) paralysis chance (wand.c:190-203).
  - Firebolt lights flammable cells along the bolt.
  - Fix: skip Lightning and Avalanche when the target is adjacent (Avalanche when `distance <= 1+lvl/3`), and Firebolt when the hero's cell is flammable.
- **B4, emergency() adjacency:** it counts sleeping and fleeing mobs as adjacent threats (775), so it chain-spends escape items. See gap 3.
- **B5, dew vial at 708:** this kills the auto-revive. See gap 7.
- **B6, fairness: want_buy:** it tests `k == IK_POT_HEALING` on the true kind (1513), so it buys the unidentified Healing out of 4 identical-looking 200g potions. Fix: when Healing is unknown, buy unknown potions only if gold allows all of them (or skip them).
- **B7, fairness: wand charges:** ranged_try, kit_fight and attack_wand_ready read `ench` for wands without IF_CHARGE_KNOWN (751, 845, 1020). A human sees "?/max". Fix: track zaps and recharge turns per wand uid, or zap and accept the fizzle (it costs a turn and sets IF_LEVEL_KNOWN).
- **B8, hero_def_est (787):** it uses raw `defense_skill`, ignoring Ring of Evasion (1.2^L, game.c:185), the rogue's light-armour bonus and the halving while paralysed. losing_fight over-estimates incoming damage when an Evasion ring is worn. hero_dpt ignores Ring of Haste (1.1^L) and Slow. Fix: expose and call the game's `hero_defense_skill()`.
- **B9, resample weights:** they give Strength 8 and Might 2 (2212), but random generation never makes them (gen.c:52). STR arrives on a quota of about 2 per 5 floors, so the truer prior is about 0.4 STR per floor, against roughly 1.2 random potions per floor. The weights are roughly OK; just note that upg_plan already counts those STR potions separately, so they are counted twice.
- **B10, testing next to sleeping mobs:** use_items_when_safe at 1867 has no `enemy_count()==0` gate (the P path at 1802 has one). When fight() skips a sleeping mob (1306), the bot drink-tests or reads Challenge in view of it. Add the gate for the identify-by-use block.

---

# Audit: GEAR AND PROGRESSION

Port = `pebxel-c/src` (PD 1.9.1 rules **with durability**). Bot = `bot3/deepbot.c`. Evidence run: an instrumented copy of deepbot.c in my scratchpad (deepbot.c itself was not edited), 8 games HS=3 and 8 games HS=0.

## 1. Mechanics table

| Mechanic | Game (source) | Bot status | Impact |
|---|---|---|---|
| Weapon tiers, STR = 8+2t, min = t+lvl, max = (t²−t+10)·DLY/ACU + lvl·t | items.c:72-89, item_defs.inc:4,19-29 | HANDLED: gear_cost/hp_cost_vs use weapon_min/max (1189) | – |
| Accuracy ACU/1.5^enc, delay DLY·1.2^enc | game.c:170-176,198-204 | HANDLED: hp_cost_vs 1192 | – |
| Excess STR: +U(0,ex) per **melee** hit (huntress: missiles only) | game.c:191-196 | PARTIAL: hero_dpt excludes huntress (792), **hp_cost_vs doesn't** (1193) | LOW |
| Armor DR = tier·(2+effLvl+glyph?1:0), STR = 7+2t | items.c:90 | HANDLED: armor_dr | – |
| Armor enc: evasion /1.5^enc, move 1.3^enc slower, swap costs 2 moves | game.c:182-189,205-211; use.c:404-418 | HANDLED: gear_cost 1206, hero_def_est 787 | – |
| Rogue: +(STR−armorSTR) evasion in light armour; Freerunner ×2 | game.c:183-185 | MISSING (hp_cost_vs, hero_def_est) | MED (rogue) |
| Hit roll P(U(0,a) ≥ U(0,d)); unseen mob def = 0 | game.c:989-994 | HANDLED: hitp/phit | – |
| DR roll U(0,DR) | game.c:1018 | HANDLED (mean DR/2) | – |
| Random gear: 60% +0, 13.3/4.4/2.2% +1..+3, same cursed −1..−3 | gen.c:11-19 | HANDLED: ev_cost P[] (1233) | – |
| Equip reveals curse; cursed can't come off | use.c:404-417 | HANDLED: cursed_known, ev_cost | – |
| ID by use: weapon 20 hits, armour 10 hits taken | game.c:302-317 | PARTIAL: ev_cost assumes "swap back if it disappoints", but the level only shows after 10/20 hits | LOW |
| Scroll of Identify | use.c:252 | HANDLED: worn first (514) | – |
| Remove Curse uncurses whole pack (not ID) | use.c:140-146 | HANDLED: reads only when a worn item is cursed (628) | – |
| **Durability**: lvl>0 gear loses 1 per hit (weapon) or hit taken (armour), max 5·(16−lvl) / 6·(16−lvl); broken counts as +0, STR stays reduced | items.c:60-68; game.c:294-301 | MISSING: no `dur` logic anywhere in decision code | **HIGH** |
| Repair: SoU on a broken item **only fixes it (no +1)**; Enchantment, Weightstone, smith fix; Tempering/Auto-repair polish | use.c:269-273,260; hero_impl.h:544; ench.c:87,238 | MISSING | **HIGH** |
| SoU: +1, uncurse, fix; **erases enchant/glyph with p=(L−1)/L** | use.c:263-275 | PARTIAL: strategic_upgrade (Monte-Carlo playouts) sees it implicitly; upg_plan/gear_cost don't | MED |
| Scroll of Enchantment: uncurse, re-roll ench/glyph, fix; glyph = +1 level of DR | use.c:257-261; items.c:90 | PARTIAL: always the weapon (524) | MED-HIGH |
| Weapon enchants (fire, poison, grim, stun, leech, chill, shock, unstable, eldritch, lucky, tempered) | ench.c:42-90 | MISSING from gear_cost (ench ignored) | MED |
| Glyphs: bounce, affection (charms **you**), anti-entropy (sets **you** alight), multiplicity (hurts you up to HT/6), potential (shocks you), metabolism (costs food), stench, viscosity, displacement, entanglement (roots you), auto-repair (costs gold) | ench.c:165-241 | MISSING: only armor_dr's +tier; harmful glyphs never weighed | MED |
| Weightstone: speed (DLY×0.6) or accuracy (×1.5), fixes | hero_impl.h:527-545; game.c:175,203 | PARTIAL: applied at once (627, picks speed); **IF_FAST/IF_ACCURATE ignored by hp_cost_vs** | MED |
| Armor kit → class armour: STR and **DR() frozen**, can never be upgraded again | hero_impl.h:500-511; items.c:47 | WRONG: applied at once to whatever is worn (626) | **HIGH** |
| Upgrade/STR quotas: SoU 3/6/9/12/13 by d5/10/15/20/25; STR potions 2/4/6/8/9 by d4/9/14/19/24 | Dungeon.java:283-291; content.c:341 | HANDLED: upg_plan SQ/PQ (493) | – |
| STR potion / Might | use | HANDLED if known (617); depends on the potion-ID domain | MED |
| XP: max_exp = 5+5·lvl; each level +5 HT, +1 att, +1 def; no XP from mobs above max_lvl | game.c:158,517-529,538 | HANDLED implicitly | LOW |
| Tome of Mastery (Gladiator/Berserker, Battlemage/Warlock, Assassin/Freerunner, Sniper/Warden) | hero_impl.h:446-487; game.c:1025-1034,1145 | PARTIAL: always option 0 (562) | MED (mage) |
| Class kits: warrior short sword +1 STR; mage knuckles + MM wand; rogue dagger + Shadows; huntress dagger + boomerang, 15 HP | newgame.c:37-50 | PARTIAL: **mage never changes weapon** (648) | MED |
| Blacksmith reforge: two identified, uncursed, lvl≥0 items of the same kind → better one +1 | caves.c:131-160 | PARTIAL: uses a pair if one happens to exist; never keeps or identifies one (run39: done 2, rewarded 0) | LOW-MED |
| Ghost reward: identified best-of-4 weapon or best-of-4 armour | content.c:367-372; sewers.c:66 | WRONG: picks by worn levels (566) | MED |
| Statue: identified, enchanted, uncursed melee weapon | mobs.c:224-233 | MISSING: passive statues skipped (1301) | LOW-MED |
| Shop d6/11/16/21: identified +0 weapon and armour of tier 2/3/4/5, weightstone; buy price = price·5·(d/5+1) | content.c:69-87; hero_impl.h:14-50 | PARTIAL: want_buy uses the armor_score heuristic, never weapons or weightstones (1510) | LOW-MED |
| Selling unknown gear | 537-544 | PARTIAL: unknown items scored +0 | LOW |
| Ring durability: 100·(16−lvl) turns worn; broken counts as +1 | items.c:63-67 | MISSING (HS rings break at d3-4 in every HS game) | LOW-MED |

## 2. TOP GAPS

### G1. Armor kit freezes broken or low armour (HIGH)
`use_items_when_safe:626` applies the kit as soon as it is in the bag. ClassArmor copies `armor_dr(it)`, which uses effective level (0 when broken), and STR, and can never be upgraded again (hero_impl.h:509, items.c:47).
**Seen:** in the HS warrior game, mail armour that had taken about 7 upgrades broke at d19 s3800. The kit went on at d20 s3943, giving a class armour with **DR 6 for d20-25**. Unbroken it would have been 3·(2+7)=27. The HS mage game froze cloth +5 at DR 7.
**Fix (in 626):** apply only if all of these hold:
- the worn armour is not `item_broken`;
- no Scroll of Upgrade, Scroll of Enchantment or Weightstone is held;
- no unknown armour in the bag is wearable (STR ≤ hero_str()+1).

If the armour is broken, first read Scroll of Enchantment or Upgrade on it (G2). There is a case for never applying the kit at all, comparing DR only: the bot never uses the class special (`ACN_SPECIAL` appears 0 times in deepbot.c). Weighing it: only one SoU spawns after d20, so the kit costs little in upgrades. The frozen DR, though, is the armour for d21-25.

### G2. Durability: wear, breakage and repair are invisible to the bot (HIGH)
A +N weapon breaks after 5·(16−N) hits, and a knuckleduster (DLY 0.5) wears twice as fast per turn. Armour breaks after 6·(16−N) hits taken. Broken gear acts as +0.
**Measured** share of steps a positive-level worn item was broken:
- HS warrior: weapon 24%, armour 8%.
- HS mage: **weapon 61%, armour 24%**.

Scroll of Upgrade on a broken item **only repairs it** (use.c:270). The mage did exactly that: cloth armour broke at d19 s4125, and SoU went on it at s4145, losing the +1.
The player can see this: "going to break soon" is logged at 1/6 durability when the level is known, the item shows "broken", and hits can be counted. Reading `it->dur` when `IF_LEVEL_KNOWN` is set is fair.
**Fix:**
- (a) `strategic_upgrade` / `pick_for(PICK_UPGRADE)`: if worn gear with lvl > 0 has `dur ≤ item_max_dur/6`, upgrade it *before* it breaks. That gives +1 and a full repair.
- (b) If it is already broken, use Scroll of Enchantment or Weightstone on it first. Use SoU only when nothing else can repair it, and value that SoU as restoring all N levels.
- (c) In the PICK_ENCHANT branch (524), pick the broken worn item first.
- (d) `upg_plan`: for a broken t, model the result as `dur = max` with `lvl` unchanged, not `lvl+1` on a still-broken copy. The copy at 498 keeps `dur = 0`, so gear_cost sees no gain from repairing.

### G3. ev_cost reads an *unidentified* Scroll of Upgrade, then treats a cursed outcome as free (HIGH, confirmed death)
At 1236, `bool up = find_kind(IK_SCR_UPGRADE) >= 0` has no `item_known` check. With `up`, cursed outcomes are clamped to `base` ("not stuck").
**Seen:** HS=0 game 4, warrior, d4, STR 12. The bot picked up unknown scroll "ODAL" at s452 and equipped unknown **plate armor of anti-entropy** at s453 (line 647). The previous step NEEDDBG had rejected it (ratio 2.27). The plate was −1 and cursed: STR 18 (enc 6) means 4.8× slower movement and evasion /11. Its anti-entropy glyph then set the hero alight, burning the scroll. The bot died at s473.
**Fix:**
- `up = find_kind(...) >= 0 && item_known(IK_SCR_UPGRADE)`.
- Treat SoU as an uncurse only with a cost. Charge the stuck outcome at least `base + value_of_one_SoU`, or just drop `up` from the not-stuck rule.
- Add `find_kind(IK_SCR_ENCHANT)` (known) as an uncurse source, alongside `have_rc()`.
- Give harmful glyphs (anti-entropy, potential, multiplicity, affection, entanglement) a penalty.

### G4. Scroll of Enchantment always goes to the weapon (MED-HIGH)
`pick_for` at 524 picks `wpn ? -1 : -2`. The warrior game re-rolled its short sword three times (blazing → venomous → blazing), while the mail armour never got a glyph. A glyph is +tier DR for life: mail +3, plate +5, kept through the class armour.
**Fix:** pick in this order:
1. a cursed or broken worn item;
2. armour with `ench == 0`;
3. a weapon with `ench == 0`;
4. armour whose glyph is in the harmful set;
5. otherwise hold the scroll.

Never re-roll a weapon that already has an enchantment.

### G5. The mage never changes weapon, and Battlemage is useless to it (MED)
Line 648 is `if (bw >= 0 && (h->hclass & 3) != 1)`. The mage wore a knuckleduster all game: 0 equips in the HS mage run, and 61% of steps broken. Meanwhile strategic_upgrade upgraded a **sword it would never wield** (d11 s2765, wasted SoU).
The Tome choice takes option 0 (562), which is **Battlemage**. That only helps when the hero melees with a wand in hand (game.c:1030), and the bot never does.
**Fix:**
- Remove the class gate, or let the gear model decide, and keep the excess-STR bonus that makes fast weapons good.
- In wnd_for, when `g_trade == 3`, choose the Tome option by class:
  - mage → Warlock (index 1): heals 1+(d−1)/5 per kill plus food, all passive;
  - rogue → Freerunner (index 1): ×1.6 speed and better evasion, a strong escape for this bot. This needs light armour; otherwise take Assassin;
  - huntress → Sniper;
  - warrior → Gladiator.
- In strategic_upgrade, exclude weapons the class gate would never wield.

### G6. Upgrades destroy enchantments and glyphs, and gear_cost ignores enchantments (MED)
Upgrading gear at level L erases its enchantment with probability (L−1)/L (use.c:263). The warrior's **vampiric mace** at d15 became a plain "mace" at d16. For glyphed armour, that loss cancels the whole DR gain: at L=3, the expected gain is tier/3 instead of tier.
**Fix:** in `upg_plan`, use `t.ench = 0` with probability (L−1)/L by averaging two gear_cost evaluations. Add a crude weapon-enchant term to `hp_cost_vs`, for example `dmg *= 1.25` for fire/leech/shock/luck and `dps *= 1.2` for stun/chill/eldritch.

### G7. Weightstone imbues are not in the model (MED)
`hp_cost_vs` (1191) ignores `IF_FAST` (delay ×0.6) and `IF_ACCURATE` (accuracy ×1.5). A weightstoned weapon is underrated by up to 40%, so the bot may swap it away and lose the stone. The stone is applied immediately (627), even to a weapon about to be replaced.
**Fix:** two lines in hp_cost_vs: `if (w->flags & IF_FAST) dly *= 0.6; if (w->flags & IF_ACCURATE) acc *= 1.5;`. Then hold the weightstone until the weapon is broken (it repairs) or the bot has kept the same weapon for a floor.

### G8. Ghost reward is chosen blind (MED)
Line 566 compares `G.hero.weapon.lvl > G.hero.armor.lvl`, which uses true levels even when they are unknown. The window actually offers two **identified** items: `G.q.ghost_weapon` and `G.q.ghost_armor`, each best of 4, often +1..+3.
**Fix:** compute `gear_cost(&q->ghost_weapon, believed armour)` and `gear_cost(believed weapon, &q->ghost_armor)`, adding `s_str_plus` for the STR still to come in the region (as upg_plan does). Pick the lower.

### G9. Smith, statues and shops: missed +1s and identified gear (LOW-MED)
- Smith (546, 563): the bot never deliberately keeps a duplicate and identifies it. Rings and wands qualify too. Its junk dropper discards worse duplicates.
  **Fix:** while `q->smith == QS_DONE`, don't junk a same-kind duplicate of worn gear, and spend Scroll of Identify on it.
- Statues (1301) hold an identified, enchanted, uncursed weapon. Kill one when the duel maths (`losing_fight`) says the hero wins.
- `want_buy` (1514): use gear_cost, not armor_score, and consider shop weapons (identified +0, so no risk). Buy a weightstone when gold ≥ price + 300.

### G10. Rogue light-armour evasion and huntress melee (MED for those classes)
`hp_cost_vs` and `hero_def_est` lack the rogue's `+max(0, STR − armorSTR)` defense (game.c:183). That bonus doubles for a running Freerunner. The bot over-values heavy armour for rogues. Huntress melee gets **no** excess-STR damage, but hp_cost_vs adds it (1193).

## 3. BUGS (wrong model or fairness leaks)

1. **ev_cost peeks at an unknown SoU** and treats it as a free uncurse (1236). This is a confirmed death cause (G3).
2. **upg_plan uses hidden levels:** `if (!(t.flags & IF_LEVEL_KNOWN)) t.lvl = t.lvl > 0 ? t.lvl : 0;` (498) reads the true positive level of unidentified gear. `base` and `bp` use raw `h->weapon` and `h->armor` (489, 502) instead of `believed(...,0)`. Should be `t = believed(it, 0)`.
3. **upg_plan on a broken item:** the copy keeps `dur = 0`, so `+n` shows no gain. The real effect is a full repair with no +1 (G2).
4. **Ghost reward choice** reads true unknown levels and ignores the offered items (566).
5. **strategic_upgrade / strat_playout** fork the real game (2275), so playouts know the true levels and curses of unidentified gear. Upgrading an unknown cursed piece "reveals" itself in the playouts. Fix: before forking, set every non-`IF_LEVEL_KNOWN` item's lvl to a draw from the ev_cost distribution, and clear IF_CURSED for those not CURSED_KNOWN at the same odds.
6. **hp_cost_vs gives huntress excess-STR melee damage** (1193). hero_dpt (792) gets it right.
7. **IF_FAST / IF_ACCURATE are ignored** in hp_cost_vs, hero_dpt and weapon_score (G7).
8. **Armor kit** applied to broken or low armour (626) (G1).
9. **`underpowered()` (1220) is dead code:** declared and defined, never called.
10. **`believed(it, unk)` with unk=−2** (639) never takes effect for unid items: they go through ev_cost, and cursed-known items are skipped. The comment at 1209-1211 describes a rule that no longer applies.
11. **armor_score / weapon_score** (still used for shop buy/sell, junk and the drop-worst list) ignore durability, enchantments, imbues and excess STR. Weapon_score ignores the excess-STR bonus that favours fast weapons. They can sell or drop a better item. Route them through gear_cost.
12. **Mage weapon gate** (648) plus Tome option 0 (562) means Battlemage with knuckles (G5).

Risks: G2a (upgrade at 1/6 durability) is safe. G4's harmful-glyph list is a judgement call: affection and displacement are double-edged. G1's "don't apply the kit" loses nothing today, because the bot never uses the class special.

---

# Audit: dungeon features, rooms and quests

Port = `pebxel-c/src` (cited as file:line). Bot = `bot3/deepbot.c` (cited as `db:line`).

## 1. Mechanics table

| Mechanic | What the game does | Bot status | Impact |
|---|---|---|---|
| Special-room odds | Each room with one connection: chance 1/(k²+2) to become the k-th special; depth%5==2 prefers Laboratory (level_gen.c:486-535) | n/a | – |
| Item prizes | Every floor builds a list of items that must spawn: food, PoS (2 per 5 floors), SoU (3 per 5), SoE, plus room keys and potions. Storage, pit, pool, trap, lab and library rooms each draw from it with chance n/(n+1) (content.c:28-33, 338-343) | Not modelled | HIGH (see gaps) |
| Armory | Locked door; 3-4 weapons/armour, 1/6 bombs (level_gen.c:294, content.c:107) | HANDLED: keys and locked doors (db:1141-1146) | LOW |
| Crypt | Locked; one tomb holding the best of 4 armour rolls. Opening it spawns a wraith on each free orthogonal cell around the hero, up to 4 (content.c:116, mobs.c:274-281) | HANDLED: opens it from the cell with the fewest open sides, gated on an attack wand or lvl≥depth+3 (db:976-985) | MED |
| Graveyard tombs (standard room) | 2-4 tombs; one random prize, the rest 1 gold. Every tomb spawns up to 4 wraiths with EXP 0 (level_gen.c:138, content.c:345) | PARTIAL: treated the same as the crypt tomb | MED |
| Skeleton remains | Wraith (or HP/2 damage) only if the opened heap's item is cursed (mobs.c:282-290, hero_impl.h:135) | PARTIAL: every skeleton is deferred behind the tomb gate (db:877, 978) | MED |
| Garden | High grass. 50%: honeypot; else 1-2 Sungrass plants. Inside with no enemy in sight: invisible, hunger 1/3 slower (content.c:117, mobs.c:69-83) | MISSING: never targets Sungrass plants; never rests in the garden | LOW |
| Laboratory + alchemy pot | Locked; 2-3 potions. 3 seeds make a potion: 1/count chance it is random, else weighted by seed (Sun→Heal, Fire→Flame, Ice→Frost, Sorrow→Toxic, Dream→Invis, Earth→Para, Fade→Mind) (trap.c:193-211) | PARTIAL: cooks the first 3 seeds in slot order (db:533, 1913) | LOW |
| Library | Locked; 2-3 scrolls (content.c:139) | HANDLED | LOW |
| Magic well | Kind is visible (journal, mobs.c:47). Health: full HP, cure, **uncurse all equipped**, hunger→0; a thrown dew vial fills it. Awareness: identify equipped, curse-know the whole pack, **reveal every secret door/trap**, show all heaps. Transmutation: swap weapon pairs, SoU↔SoE, PoS↔Might, reroll ring/wand/seed (trap.c:213-282) | PARTIAL: throws worse weapons into Transmutation (db:693-699); other wells are only stepped on at floor end, whatever the HP. The "well" branch of plan_next is dead code (bug B3) | MED |
| Pit room | Reached only by falling through the weak floor above. Locked door, key inside. Skeleton with key, 20% ring or gear, and 1-2 prizes (content.c:149-157, floor.c:494-519) | MISSING: never jumps (db:2735) | MED |
| Pool | Water, pedestal prize (best of 5, or a list prize), 3 piranhas (HP 10+5d, att 20+2d); Invisibility potion spawned elsewhere (content.c:159-170) | HANDLED: dry pathing, invisibility/levitation, exits fast (db:986-1004, 1857-1866) | MED |
| Storage | **Barricade (or bookshelf 1/3)**, needs fire. 3-4 items, many of them list prizes; Liquid Flame spawned elsewhere (level_gen.c:368-370, 618; content.c:172-178) | MISSING: never burns anything | HIGH |
| Statue room | Locked; passive statue (HP 15+5d, DR=d, def 4+d) holds an enchanted, identified, non-negative melee weapon and drops it on death (mobs.c:224-234, 151-158) | MISSING: passive statue skipped (db:1300-1301) | MED |
| Traps room | Filled with toxic (3/6), paralytic (2/6) or chasm (1/6; summoning before a boss). Prize on a pedestal or chest; Levitation spawned elsewhere (level_gen.c:372-386) | HANDLED: springs traps from the doorway with spare items, levitation (db:939-969) | MED |
| Treasury | Gold; 50% chests, where the **first chest is always a mimic** (content.c:183-191) | HANDLED as an ordinary fight | LOW |
| Vault | Locked door; locked chest (golden key), 2 crystal chests (one golden key; contents visible), or a pedestal (content.c:193-206) | PARTIAL: opens the nearest crystal chest, no choice made | LOW |
| Weak floor | Chasm room; falling lands you in the next floor's pit room (level_gen.c:401-407) | MISSING | MED |
| Altar | Fire marks creatures for 5 turns; kills feed it exp×1-3 until 5+5d, then it gives Wipe Out (mobs.c:85-101, content.c:208) | HANDLED (altar_lure db:820-837, Wipe Out db:777, 859) | LOW |
| Shop (6/11/16/21) | Tier weapon and armour, bag, weightstone (torches at 21), PoH + 3 potions, SoI/SoRC/SoMM + 1 scroll, 2 food, Ankh. Price = base ×5×(d/5+1): ×10/15/20/25; Haggler halves it. Selling pays base (content.c:69-101, hero_impl.h:23-57, 335) | PARTIAL: buys bags > food > PoH > Ankh; sells worse gear only (db:1510-1516, 1890, 537-545) | MED |
| Blacksmith (12-14) | 15 dark gold (each mine: 2 turns, **+36 hunger**: 540 in total) or bat blood (kill a bat while wielding the pickaxe). Reward: reforge 2 identified, uncursed, ≥0 items of the same kind → +1 (caves.c:87-150, use.c:440-451) | PARTIAL: gold variant handled; blood variant broken (bug B2) | MED |
| Ghost (2-4) | Rose (one of the rat/gnoll/crab kills drops it: 1/8, 1/7, …), fetid rat (paralytic cloud whenever hit, sewers.c:171), or curse (accept → Curse Personification). Reward: best-of-4 weapon or armour, identified (content.c:360-374, sewers.c:13-82) | PARTIAL: bounces on the stairs for kills (db:1957-1963); curse window bug (B1) | MED |
| Wandmaker (7-9) | Rotberry (stepping on it: toxic gas + 3 turns rooted; dropping an item on it: no roots), corpse dust (a cursed skeleton, so a wraith), phantom fish (hidden in water, found by searching). Reward: battle or utility wand, +1..+3 (content.c:284-302, prison.c:96-160) | PARTIAL: fish and rotberry tasks exist (db:1542-1551); corpse dust only via the tomb gate | MED |
| Imp (17-19) | 8 monk tokens or 6 golem tokens, dropped by kills **anywhere** after the quest is given; reward ring +2, sticky (city.c:44-81, content.c:303-315) | PARTIAL: never climbs back to hand in tokens | LOW |
| Traps (hidden, 1..n_rooms+d per floor) | Toxic 300+20d, fire, paralytic 80+5d, poison 4+d/2, alarm (all mobs come), lightning (1/3-2/3 of **current** HP, never kills, +1 charge to every wand), gripping (bleed+cripple), summoning 1-3 mobs. Every trap then goes dead (trap.c:9-69, level_gen.c:671-681) | HANDLED: trap_danger ranking, throws an item to spring a blocking trap (db:1556-1574, 2030-2034) | LOW |
| Burned rooms | Floor is half hidden fire traps (level_gen.c:125-137) | HANDLED (burnt_scan db:249) | LOW |
| Secret doors | 1/(12-d) chance per door below depth 6, then 1/6. An intentional search always finds everything in the 8 cells it sees (2 turns); each step has a passive chance (level_gen.c:611, game.c:1082-1112) | HANDLED (search_spot2, evidence_hidden db:1595, 1645) | LOW |
| Chasm fall | Damage HT/3..HT/2, crippled 10 turns, land on a random respawn cell of d+1 (or the pit room); a confirm dialog comes first (game.c:1128-1133, 1183; floor.c:516-531) | MISSING (no escape, no shortcut) | LOW-MED |
| High grass / dew | Trampling gives a seed 1/18 and dew 1/6 (more with Herbalism); grass blocks sight (game.c:1118-1124) | Ignored | LOW |
| Dew vial | A **full vial (10) auto-revives on death** with +HT (game.c:505); drinking heals 41% at 5 and 100% at 10 (use.c:355-361) | Drinks it at ≥5 or ≥3 (db:708, 774), which spends the revive | MED |
| Water | Puts out fire, washes Ooze off, keeps piranhas in | HANDLED | – |
| Respawner | One spawn on arrival, then every 50 turns (**25 at night (host clock <7 am) or with the Amulet**), up to 2+d%5+rnd(3) alive (game.c:780-791, 962; floor.c:470-475) | HANDLED (stair bounce) | – |
| Keys | Per-depth counts; the skeleton key comes from the boss, the iron key for Tengu's arena from a chest in the anteroom (content.c:269-276) | HANDLED (db:1986-1993) | – |
| Mimic chests | 1 in 20 random heaps from depth 2 on (20% of chests); HP (3+d)×4; knocks gold loose (content.c:379, mobs.c:222, 178) | Opens every chest whatever its HP | LOW |
| Level feeling / "secrets" hint | A log line when secret_doors > 3-4 (floor.c:467) | Unused | LOW |
| Amulet ascent | Pedestal on 26. With the Amulet every creature is beckoned on arrival and respawns come twice as often (floor.c:473, game.c:962) | HANDLED: straight up (db:1880). Lloyd's Beacon wasted (bug B4) | HIGH (beacon) |

## 2. Top gaps

**G1. Burn barricades (storage rooms). HIGH.**

- **Why:** a storage room holds 3-4 items, and each draw takes an item from the must-spawn list with chance n/(n+1) (content.c:31, 175). The floor's PoS or SoU therefore usually ends up behind the barricade. Over a game, expect about one permanent upgrade lost to these rooms.
- **Fire sources:** a known Liquid Flame (spawned on that floor, content.c:176), an Incendiary Dart (lights flammable ground, hero_impl.h:243), a Wand of Firebolt, or a Firebloom seed planted beside the barricade and stepped on (needs water nearby).
- **Rule:** in `puzzle_task`, find a known `T_BARRICADE`/`T_BOOKSHELF` in a seen RT_STORAGE wall with a reachable cell 2-3 away in line.
  - With a known flame potion, an incendiary dart or a fire wand in hand: `fire_safe(hero)`, no scroll stash at risk, scrolls in the holder or dropped (reuse `stash_for`). Then `game_throw` or `game_zap` at the barricade cell, and `game_rest` until `g_blob[BLOB_FIRE]` is empty.
- **Risk:** grass fires. Reuse the existing fire-wait and water-escape code.

**G2. Lloyd's Beacon for the ascent. HIGH when held (Goo drops it 33% of the time).**

- **Problem:** db:702-706 sets or returns at random (1 in 50 per safe step). A random return teleports the bot back to an old floor and wastes food.
- **Rule:**
  - Never use it at random.
  - On the first non-boss floor after getting it, beside the stairs up with no creature adjacent: `do_item(s, ACN_SET)`. Re-set it on depth 1 if the bot is ever there.
  - When the Amulet is picked up on 26 (DF_AMULET set) and no creature is adjacent: `ACN_RETURN`.
- **Gain:** skips about 20 floors of Amulet ascent, where everything is beckoned and respawns come every 25 turns.

**G3. Use the magic wells. MED.**

The well kind is fair knowledge (journal, mobs.c:47).

- Fix bug B3 first.
- **Awareness:** drink as soon as it is reachable. It reveals every secret door and trap (saves the search phase) and curse-knows the whole pack, after which the gear model can equip the clean unknowns.
- **Health:**
  - Treat it as a free remove-curse: `have_rc()` (db:1213) should also return true while an unused, visited Health well is on this floor. Drink it when something equipped is `cursed_known`, when HP < 40%, or as the last act before the stairs.
  - If a dew vial is held with `G.dew < 10` and HP ≥ 80%, throw the vial in instead (`game_throw(vial, well_pos)`, trap.c:246). That gives a full vial, which is an extra life (G6), then pick it up.

**G4. Fix the Blacksmith bat-blood quest and gate the gold quest. MED.**

- **Bat blood:** see bug B2. The fix is to hoist the pickaxe-equip lines out of the `!PRM[P_GEARMODEL]` block.
- **Gold:** costs 540 hunger units plus about 30 turns, and the reward needs an identified same-kind pair.
  - Mine only if such a pair exists or is likely: `npc_wanted`'s pair test, or two of the same armour/weapon kind in the bag.
  - While the quest is active, stop dropping duplicate same-kind gear as junk (db:711-720).
  - Read Identify on the pair.

**G5. Statue rooms. MED.**

- **Why:** a free, identified, enchanted, non-negative weapon.
- **Rule:**
  - In `use_items_when_safe` or `puzzle_task`, if a passive `MT_STATUE` stands in a visited room and HP ≥ 90%: compare its weapon (the carried heap at `CARRIED_POS(uid)`; it is fair because info.c:197 shows it) via `gear_cost`.
  - If it is better than the wielded weapon, also require `!losing_fight(i)`. `losing_fight` already uses the overlay stats: HP 15+5d, DR=d, so low-damage weapons are weak here.
  - Then attack it with `game_tap_cell`. Remove the skip at db:1301 for this case.

**G6. Keep a full dew vial as an extra life. MED.**

- **Why:** game.c:505 revives with +HT at G.dew==10. The bot drinks at ≥5 when safe (db:708) and at ≥3 in an emergency (db:774).
- **Rule:**
  - At dew == 10, never drink (the revive is worth more than a heal).
  - Below 10, keep the current behaviour.
  - Pick up dew drops whenever the vial isn't full (target list tgt.c:272 already does this).

**G7. Jumping into chasms: the pit room and escapes. MED.**

- **Pit room:**
  - On floor d with a seen RT_WEAK_FLOOR room, floor done, HP ≥ 85%, not hungry: walk next to an interior chasm cell, `game_tap_cell(chasm)`, then `game_jump()`.
  - The bot currently just clears `s_jump`. Make it call `game_jump()` when it planned the jump.
  - You land in the pit room (key + ring/gear + 1-2 prizes). Open the skeletons and use the key on the door.
- **Escape:** in `emergency`/`kit_fight`, a losing fight with an adjacent chasm, HP ≥ 55%, and no healing → jump.
- **Risk:** the fall takes HT/3..HT/2 and cripples for 10 turns.

**G8. Shops. MED.**

- **Priority:** the Ankh (an extra life) should come before the bags (db:1890).
- **Buy Remove Curse:** 30×mult gold; it unlocks the unknown-gear model through `have_rc()`.
- **Buy the weightstone** (bot already applies it, db:627).
- **Sell more:** before buying, sell every unwanted, known-bad ring/wand and spare missile stack.

**G9. Skeletons and graveyards. MED.**

- **Separate them from the crypt.** At db:978 a plain skeleton risks at most one wraith (HP 1, EXP 0), and only if cursed. Open it at HP ≥ 80% without the level gate. This also completes the corpse-dust quest (wand_alt 1), which otherwise fails whenever lvl < depth+3 and no attack wand is held.
- **Skip graveyard tombs** (RT_STANDARD room, several tombs, EXP 0 wraiths, mostly 1 gold) unless lvl ≥ depth+5. Keep the gate for RT_CRYPT.
- **Room skip:** `s_room_skip[r]=1` on failure also hides every other puzzle heap in that room. Use `s_black[c]` instead.

**G10. Fetid rat and rotberry. LOW-MED.**

- **Fetid rat:** it releases paralytic gas 20 on every hit taken (sewers.c:171), and it is gas-immune itself. Fight it with darts from range ≥ 2, or step back out of the cloud after each blow.
- **Rotberry:** throw any `throwable()` onto the bush (trap.c:129-131 via level_drop). This gives the seed with no roots. Then wait out the gas before picking it up, instead of stepping onto it (db:1550).

## 3. Bugs (the bot's model is wrong)

- **B1. The Ghost curse quest is refused forever** (db:566). `wnd_for` applies the reward rule `weapon.lvl > armor.lvl ? 1 : 0` to every Ghost window. For the curse variant, button 1 is "No, I can't help you" (sewers.c:41).
  - Whenever the weapon outlevels the armour, the bot declines, the ghost relocates, and `npc_wanted` (db:1492) sends the bot back to repeat this for up to 600 bounce steps.
  - Fix: if `ghost_alt==2 && ghost != QS_DONE`, return 0.
  - The reward choice also ignores the actual rewards. Compare `gear_cost` with `G.q.ghost_weapon` against `G.q.ghost_armor`; they are identified and shown, so this is fair.
- **B2. The bat-blood smith quest can never finish.** The pickaxe-equip code (db:654-657) sits inside `if (!PRM[P_GEARMODEL])` (db:650-664). With PRM[P_GEARMODEL]=1 (the default, db:73) it never runs. Meanwhile `wait_quest` (db:1528) keeps the bot bouncing on that floor for 600 steps, burning food.
- **B3. The "well" target in `plan_next` is dead code** (db:1147). `bfs` never assigns `b_dist` to F_AVOID cells (db:287), and T_WELL is F_AVOID (level_rt.c:31).
  - So wells are only used at the very end through the target list (db:1912), drinking Health at full HP and Awareness after the search phase.
  - Fix: give wells a neighbour-based `b_dist`, as is done for locked doors (db:1141-1142).
- **B4. Lloyd's Beacon is used at random** (db:702-706). Returns teleport to old floors (lost turns and food). See G2.
- **B5. Seeds are treated as unidentified.** The port never names seeds generically (items.c:140-143) and never sets their known bit (no `set_known` for seeds outside the test kit).
  - The bot gates every seed tactic on `item_known(IK_SEED_*)` (db:761, 1052, 1056). Those branches never fire.
  - db:1061 instead plants "unknown" seeds blindly as a gamble.
  - Treat seeds as known.
- **B6. A planted Sungrass never heals.** The bot plants it under itself (db:701), but planting does not press the cell (use.c:367-372, trap.c:151). The bot never steps off and back on, so the seed is wasted. Follow the plant with `s_step_back` and a return step.
- **B7. The Health well's dew-vial fill and hunger reset are not valued.** The bot's only explicit well logic is Transmutation (db:694). See G3.
- **B8. Fairness peeks.**
  - `want_buy` tests `IK_POT_HEALING`, `IK_SCR_UPGRADE` and `IK_POT_STRENGTH` without `item_known` (db:1513). Harmless only when the head start makes healing known.
  - `room_prize` counts heaps the hero has never seen (db:1634).
  - The ghost window rule reads `weapon.lvl`/`armor.lvl` even when the levels are unknown (db:566).
- **B9. Environment confound.** Respawn doubles before 7 am host time (floor.c:471-472, game.c:962). Batch results depend on the wall clock. Pin `G.night=0` in the harness, or log it.

---

# Audit: MONSTERS AND BOSSES

Sources: port `src/c/game.c` (AI, combat), `src/c/mob_*.inc`, `src/ovl/{mobs,sewers,prison,caves,city,halls}`, stats from `tools/mobs.py`. I spot-checked the stats against the Java files and they all match. Bot: `/home/claude/bot3/deepbot.c`.

## 1. Mechanics table

| Mechanic | What the game does (source) | Bot status | Impact |
|---|---|---|---|
| Hit roll | `U(att) >= U(def)`; magic attacks double att (game.c:989-1007) | HANDLED: `phit` 786 is exact | – |
| Surprise (def 0) | A mob that hasn't seen you (`!MF_SEEN`) or is paralysed/frozen has 0 defence (game.c:1006) | PARTIAL: `hero_dpt` 794 counts paralysis/frost only, not unaware mobs | LOW |
| Assassin surprise bonus | +U[1,dmg) (game.c:1034) | MISSING | LOW |
| Waking a sleeper | Each mob turn: `rnd(dist + shadows + 2·levitating) == 0` (game.c:740). At distance 1 it **always** wakes | WRONG model at 1306 (thinks a nearby sleeper gives a "sure first hit") | MED |
| Wanderer notices | `rnd(dist/2 + shadows) == 0`, so certain within 3 cells; a just-alerted mob always notices (game.c:746) | not modelled; prep_retreat's "rest out of sight" fits it | LOW |
| Mob speed / turn order | move_cost 30 for crab, bat, piranha (2x speed); atk_delay: monk/thief 30, golem 90, eye 96 | HANDLED: fast-mob step-back 1457, "let it come" wait 1479, atk_delay in `foe_dpt` | – |
| Fleeing | Only Terror, a thief after stealing, a spinner after poisoning. No low-HP flee in 1.7; no champions | n/a | – |
| Rat, gnoll, crab | plain stats; crab moves 2x | HANDLED (duel maths, step-back) | – |
| Albino rat | 50%: bleed = damage dealt, decays about x0.75 per turn (sewers.c:149) | MISSING from duel | LOW |
| Swarm | Splits on each hit (clone gets half the HP, 4-way neighbour), att 12; healing drop 1/(5·(gen+1)) (mob_swarm.inc:2) | PARTIAL: generic choke logic only applies at range ≥2 (1445) | LOW-MED |
| Fetid rat (ghost quest) | Every hit on it puts 20 paralytic gas on its own cell (sewers.c:171); it is immune, you get paralysed 10 turns | MISSING: melee'd like a rat | MED (d2-4) |
| Curse personification (ghost) | Wraith with aux2=1: 10+3d HP, regen 1/turn, shoves you (sewers.c:77,93,157) | Fought normally. Window choice BUG, see §3 | LOW |
| Goo | 80 HP; pumps up 1/3 of turns (2 turns): att 30, dmg 5-30, reach 2 by jump; ooze 1/3; heals +1/turn in water; seals the entrance (sewers.c:89-128) | HANDLED (big plan 1340-1426, calm 1791). Water-heal only behind env flags | – |
| Skeleton | Death blast 3-8 minus U(0,DR/2) on all 8 neighbours, can kill you (mob_skeleton.inc:2) | MISSING | MED |
| Thief / bandit | atk 0.5 (2/turn); the first hit steals a random unequipped item (**a whole bag too**), then flees at speed 1; drops a coin when hit; cornered → fights; bandit blinds 5-12 (mob_thief.inc) | PARTIAL: Amulet only (1872); other loot dropped after `ignore_mob` 40 (1485) | HIGH |
| Gnoll shaman | Zap at range: magic hit, 2-11 dmg, x1.5 if you stand in water, no DR, 2-turn cost (mob_shaman.inc) | PARTIAL: in `ranged` list; zap and magic not in `foe_dpt` | LOW |
| Tengu | 120 HP, 8-15; shuriken from range; every 5th turn adjacent it leaps to a random cell in sight not next to you and arms 4 inactive traps as poison traps (prison.c:19-52) | PARTIAL: in `ranged` list only | MED |
| Vampire bat | 2x speed, flies, heals by the damage it deals (mob_bat.inc:2) | PARTIAL: speed yes, lifesteal not in duel | MED |
| Brute / shielded | dmg 8-18, enraged 10-40 below HT/4; loses a turn when it enrages (mob_brute.inc) | **BUG**: duel uses the table 10-40 always (§3) | MED |
| Cave spinner | 50%: poison 7-9 (≈17 total dmg), then flees laying webs; comes back once you're cured (caves.c:17-30,222) | MISSING | MED |
| DM-300 | 200 HP, DR 10; vents 30 toxic gas per act; heals U[1, missing) on each inactive trap it steps on; quake paralyses 2 turns (caves.c:33-53) | PARTIAL: no stepping when adjacent (2164), no Terror (2173), gas-fight rule 1313 | MED |
| Fire elemental | 50% sets you alight (burns scrolls, 1-5/turn); fire-immune; frost does 1..2/3 HT (game.c affect_frost) | PARTIAL: runs to water when burning (1812); kit ignores immunity/frost | MED |
| Dwarf warlock | Shadow bolt at range: magic hit, 12-17, no DR; 50% Weakness 40 turns (−2 STR) and drains 1 charge from every wand (city.c:16-37) | PARTIAL: `ranged` list | MED |
| Monk / senior | 2 hits/turn; 1/6 per hit knocks the weapon to your feet (not knuckles, not cursed); senior paralyses 1/10 (mob_monk.inc) | HANDLED: pick up + re-equip at 1825/1829 (2 turns) | LOW |
| Golem | 85 HP, DR 12, 20-40, atk 1.5 | HANDLED by stats | – |
| King of Dwarves | 300 HP; walks to the next pedestal and summons undead up to 1+5·lost/HT; you standing on the pedestal blocks it; undead 1/5 paralysis, eat toxic gas (city.c:84-176) | MISSING | MED |
| Succubus | 1/3: charm 3-7 turns (you can't melee it; zap/throw still allowed, game.c:1250); blinks to cells >2 away; sight 4 (mob_succubus.inc) | **BUG**: bot just rests while charmed (1430) | MED |
| Evil eye | Deathgaze ray through everything in line, magic hit, 14-19, no DR, every 1.6 turns, sight 4 (halls.c:23-51) | PARTIAL: `ranged` list, ambush retreat, torch | MED |
| Scorpio / acidic | Never melees: steps away when adjacent and shoots when in line (halls.c:54-76); 50% cripple 10 turns; acidic splashes U(0,dmg) of **your** hit back at you, thrown too (halls.c:202) | PARTIAL: `ranged` list; acid splash and cripple-kiting not modelled | MED |
| Yog-Dzewa | Immovable, passive; damage >> number of live fists; **every hit that lands (melee or thrown) spawns a larva** (25 HP, att 30, 15-20); wands skip defenseProc (halls.c:111-119, 213-220) | **BUG**: adjacent boss is always target #1 (1302) | HIGH (mandatory for the win) |
| Rotting / burning fist | Rotting: 300 HP, DR 15, 24-36, ooze, +4/turn in liquid. Burning: shadow bolt 20-32 at range, fire aura (halls.c:149-196) | PARTIAL: burning fist missing from `ranged` list | HIGH |
| Mimic | HP (3+d)·4, dmg HT/10..HT/4, knocks gold loose | PARTIAL: table dmg 1-5 is wrong; can't be foreseen fairly | LOW |
| Wraith | HP 1, def (10+d)·5, so melee nearly never hits | HANDLED: wands (1428), graves deferred (978) | – |
| Animated statue | Passive until hurt; carries an identified enchanted weapon (description names it), HP 15+5d, def 4+d, DR d (mobs.c:151,224) | PARTIAL: always skipped (1301): a free enchanted weapon is lost | MED |
| Piranha | Water only, 2x speed, scales with depth | HANDLED (1328-1339, 1805) | – |
| Bee / mirror / sheep / rat king / NPCs | allies / harmless | HANDLED (`ally`, sheep wait 1834) | – |
| Mob immunities (MD_I_*) | e.g. King, undead, fetid rat, piranha I_PARA; Yog, elemental I_BURN; DM-300 I_GAS | MISSING in kit choice (`kit_fight` 848-852, `ranged_try` 761) | MED |
| Regeneration in fights | Goo +1 in water, rotting fist +4, curse +1, DM-300 trap repair, bat lifesteal | MISSING in `losing_fight` | MED |
| Lookahead | Forks the real game and re-rolls the RNG and unknown potions/scrolls (2209-2273) | HANDLED as a simulator; off during the Goo fight (2250) | – |

## 2. Top gaps

**1. Yog-Dzewa / fists plan (HIGH: the run can't be won without killing Yog).**
- With 2 fists alive Yog takes ¼ damage, and every hit that lands calls `yog_defense` and spawns a larva. Over Yog's 300 HP that is dozens of 25-HP, 15-20-dmg larvae.
- Today `fight()` sets `key = -1` for any adjacent `MD_BOSS` (1302), and `decide()` walks to the boss (1988). So the bot hammers Yog.
- **Fix in `fight()`:**
  - When `m->type == MT_YOG`, skip it as a target while any `MT_ROTTING_FIST`/`MT_BURNING_FIST` is alive.
  - In the key, rank fists ahead of Yog, the burning fist first (it shoots 20-32).
  - Add `MT_BURNING_FIST` to the `ranged` list (1444).
  - Once the fists are dead, prefer zapping Yog with attack wands, even adjacent (`ranged_try` already zaps adjacent): no larva spawn.
  - Melee Yog only when no larva is adjacent. Larvae keep their normal HP key.
- **Rotting fist:** don't fight it while it stands in water (`TFLAGS & F_LIQUID`, +4/turn). Step back to a dry cell 2+ away from water and let it follow.

**2. Thieves (HIGH).**
- `random_unequipped()` can take a bag (a seed pouch, scroll holder or wand holster, with everything in it), the healing potion or the Ankh.
- The thief hits twice a turn with att 12, so if it reaches you, theft is near certain.
- What it carries is in its description (fair to know). Find it with `HEAP_CARRIED` at `CARRIED_POS(uid)`, the same way as line 1876.
- **Rules:**
  - (a) In `fight()`, give a hunting `MT_THIEF`/`MT_BANDIT` within 4 the top non-adjacent key, and spend `ranged_try`/`kit_fight` on it even when not "losing".
  - (b) Once it carries anything worth more than a few gold, keep its uid on a chase list. Don't `ignore_mob` it (1485). Remember its last seen cell, keep stepping toward it, and throw or zap at it when in line.
  - It flees at your own speed and turns to fight when cornered (`thief_act`). It never vanishes in 1.7.

**3. Duel maths: the damage model (`foe_dpt` 797, `hero_dpt` 789).** Fixes in §3. Each one moves the losing/winning verdict that drives `kit_fight`, `prep_retreat`, `s_losing` and the 60% engage gate:
- Use the overlay's damage roll.
- Model the ranged and magic attackers.
- Add per-type extra damage: poison ≈17 per spinner proc, half the time, so ≈ +8.5 per hit; burn ≈ 3×(turns) for elementals; ooze; bleed.
- Count regeneration: bat lifesteal, rotting fist and Goo in water, DM-300.
- **Concrete:** `turns = HP / max(0.1, hero_dpt - regen)`, where `regen = foe_dpt(m)` for a bat (it heals what it deals) and 4 for a rotting fist in liquid.

**4. Succubus charm (MED).**
- At 1430 the bot rests while the charmer, still adjacent, keeps hitting (15-25).
- Charm only blocks the tap-attack (game.c:1250).
- **Replace with:**
  1. `ranged_try(best)` (wands and darts work beside it).
  2. Else attack another adjacent non-charmer.
  3. Else, if HP < 50%, `emergency()`.
  4. Else step to a cell not adjacent to it. Charm ticks down either way, and its blink is on a 5-step cooldown (`BLINK_DELAY`).

**5. Fetid rat and skeleton "on-hit/on-death" traps (MED).**
- **Fetid rat:** each hit seeds paralytic gas on its cell, so a melee hero is paralysed 10 turns while it bites.
  - Rule: vs `MT_FETID_RAT`, use `ranged_try` from 2+ cells.
  - Else hit once, then step back out of the gas. `escape_air` already treats paralytic gas as bad, so a rule in `fight()` is enough: "if `blob_at(BLOB_PARALYTIC, neighbour of me)`, retreat first".
- **Skeleton:** the death blast is 3-8 minus U(0, DR/2).
  - If `m->type == MT_SKELETON && m->HP <= hero max hit` and `h->HP <= 8 - hero_dr()/4`, finish it with a throw or zap from 2+ cells.
  - Or step back and let it come while you heal, or drink first.

**6. Spend the kit by immunity and weakness (MED).**
- In `kit_fight` (848-852) and `ranged_try` (761), skip items the target is immune to:
  - curare / paralytic vs `MD_I_PARA` (King, undead, fetid rat, piranha);
  - flame / incendiary vs `MD_I_BURN` (elemental, Yog, piranha);
  - toxic gas vs `MD_I_GAS` (DM-300, Yog).
- Prefer a known Potion of Frost vs `MT_ELEMENTAL`: it deals 1..43 and puts it out.
- These are table flags, so it's fair knowledge.

**7. Scorpios and eyes (MED, halls).**
- A scorpio steps away whenever adjacent and shoots when in line. Each hit has a 50% chance to cripple you for 10 turns, which doubles your move cost, so chasing in the open is a losing loop.
- **Rule:**
  - vs `MT_SCORPIO`/`MT_ACIDIC` at range, with no wand or missile, step to the nearest cell that breaks its line (`ballistica(m->pos, c) != c`) next to a door or corner and rest there. It loses sight, wanders and comes back round within 3, where you get a blow.
  - Never chase while crippled.
- **Acidic scorpio:** every hit you land costs you U(0,dmg). Prefer wands. Add `0.5*hero_dmg` to the cost side of the duel.
- **Eye:** same line-breaking approach. Its ray passes through other mobs, so a mob between you and it doesn't shield you.

**8. Statues (MED, gear).**
- The weapon is identified and enchanted, and the statue's description names it.
- If `weapon_score(carried item) > weapon_score(current) + margin` and `!losing_fight(i)` (with the statue's stats via `mst`: DR = depth), attack it.
- Today line 1301 skips it forever.

**9. King of Dwarves (MED).**
- The King only summons when standing on the next pedestal `pedestal(!aux2)` and the army is under 1+5·lost/HT.
- **Rule:** fight the King when he's in melee. When he heads to an empty pedestal while the army is under the cap, step onto that pedestal if it's reachable before him (`can_summon` is false while you stand there).
- Kill undead (28 HP) only when the King is not adjacent.

**10. Surprise exploitation (LOW-MED).**
- A thrown missile at a sleeping or unaware mob always hits (def 0). An Assassin adds about +50% damage.
- **Rule:** when the bot means to engage a sleeper (1306), open from 2+ cells with a cheap missile (`throwable()` darts) instead of walking up. Walking adjacent wakes it for certain (`rnd(1)==0`).

## 3. Bugs in the bot's model

1. **`foe_dpt` uses the table damage, not the overlay (797-799).** `MOB_DEFS.dmg_lo/hi` is wrong for:
   - brute/shielded: assumed mean 25, really 13 until enraged, so the bot is ~2x too afraid of brutes;
   - Goo pumped (5-30 vs 2-12);
   - mimic (HT/10..HT/4 vs 1-5);
   - statue (its weapon);
   - piranha (d..4+2d vs 1-5);
   - curse personification (3-5);
   - wraith (1..3+d).

   `gear_cost`/`hp_cost_vs` (1198) has the same brute error. **Fix:** save `g_rng`, average 16 calls of `g_mh.stats(i, STAT_DMG)` (falling back to the table when it returns −1), then restore `g_rng`. That's exact and consumes no game randomness.
2. **Ranged and magic attacks are missing (797).**
   - Shaman, warlock, eye and burning fist use `char_hit(...,magic=true)`: att x2, and their damage skips your DR.
   - Shaman 2-11 (x1.5 in water), cost 2 turns. Warlock 12-17. Eye 14-19 every 1.6 turns. Burning fist 20-32.
   - The bot applies DR and single att. Example: eye (att 60 effective) vs def 20 hits 83%, not 50%.
   - `losing_fight` also only sums hunters within distance 3 (806), so a shaman at 5 or an eye at 4 adds nothing.
3. **`hero_def_est` (787) omits:**
   - Ring of Evasion (x1.2^level);
   - the rogue's light-armour bonus and the Freerunner's (game.c:182-188).

   With a +3 evasion ring, incoming damage is overestimated about 1.7x. Mirror `hero_defense_skill` exactly (it is `static` in game.c, so copy it).
4. **DR as a flat mean (793, 799).** `E[max(0, X−U(0,DR))]` is larger than `mean−DR/2` whenever they overlap. Against high-DR mobs (scorpio 16, rotting fist 15, King 14, golem 12) the bot under-rates its own damage and clamps it at 0.3. Compute the expectation by summing over U(0..DR) (≤17 terms).
5. **Cost sum (810).** `turns_total * dpt_all` counts every foe's damage for the whole fight, about 2n/(n+1) too pessimistic for n foes. The comment admits it. The honest sum is `Σ_k dpt_k * (time until k dies)` with kills in key order.
6. **Fairness.**
   - `losing_fight` (806-809) reads `state` and `HP` of mobs within 3 that are not in FOV.
   - `fight()` fights unseen adjacent hunters (1299). That's acceptable (you feel the hits), but the 806 loop should require `BIT_GET(G.fov, o->pos) || adjacent`.
   - Lookahead playouts run the real hidden state (unseen mobs, mimics, hidden traps); only the RNG and unknown items are re-rolled (2209-2234).
7. **The sleeper "sure first hit" assumption (1306) is false.** The mob gets an act at distance 1 and always wakes (game.c:740, `rnd_int(1)`), unless you have Ring of Shadows or levitation. Your first blow then meets full defence.
8. **The adjacent key (1302) is `HP` only, and a boss is always first.** Besides the Yog bug, a better key is threat per turn of work: `foe_dpt(m) / (m->HP / hero_dpt(m))`, highest first. That kills a monk or thief before a half-dead rat.
9. **Charm handling (1430) rests while being hit.** See gap 4.
10. **Ghost window (566).** It returns `weapon.lvl > armor.lvl ? 1 : 0` for every ghost window.
    - On the curse offer ("Yes"=0 / "No"=1), it declines whenever the weapon level is higher, so the quest never starts.
    - It also reads `lvl` of possibly unidentified gear (a peek).
    - Use the button text (`s_wnd_b1`) to tell the windows apart, and `known_lvl()`.

---

# Audit: hero mechanics, buffs and time

Port: `pebxel-c/src` (G = game.c). Bot: `bot3/deepbot.c` (DB). 1 turn = 1 TICK.

## 1. Mechanics table

| Mechanic | What the game does (source) | Bot status | Impact |
|---|---|---|---|
| Hunger rate | +10 every 10 turns (1/turn). A rogue's tick comes every 12 turns. A Ring of Satiety takes L off each tick. Hungry at 260, starving at 360 (G:631-640, 948) | PARTIAL. The bot never models it as a budget | HIGH |
| Hungry status | No penalty at all, only a warning | HANDLED (DB:1776 comment) | — |
| Starving | Hunger stops rising. 30% chance per 10 turns of 1 damage (0.03 HP/turn). **No regeneration** (G:633-634, 950) | HANDLED (eats at once, DB:1776) | HIGH |
| Food energy | Ration 260, pasty 360, meat/carpaccio/chargrilled/overpriced 100. Eating takes 3 turns. A warrior heals 5; a mage's wands each gain 1 charge (use.c:316-321) | PARTIAL. Ignores the warrior/mage bonuses | LOW |
| Food supply | One FOOD item per non-boss floor (ration 80%, pasty 20%: about 280), dropped in a **STANDARD room** (content.c:336, 14-20). Bosses, d21 and d26 have none | MISSING (no "this floor's food not yet found") | HIGH |
| Stairs | Each descent or ascent costs +36 hunger unless starving (G:1280; also mining, ench.c:200) | MISSING in the bounce and stair-rest costs | MED |
| Regeneration | +1 HP per 10 turns at any level, faster with Ring of Mending (1.2^L). Stops only when starving (G:949-952) | PARTIAL. Rests at 50→80, 40→65 with no food, 25→40 with a backup (DB:1843-1855) | HIGH |
| Rest vs search | Rest is 1 turn. Search is 2 turns (4 if it finds something), finds every secret within 1 for certain, and regenerates the same. Walking also regenerates (G:1365-1379, 1082-1108) | MISSING (resting never searches) | MED |
| Sungrass | Heals HT/10 every 5 turns (20x regeneration) while standing on it. **It triggers only when stepped onto.** Planting under yourself does not trigger it (trap.c:111-113, 140-155; G:954-957) | **BUG** (DB:701) | HIGH |
| Garden "Shadows" | In a garden room with no enemy in sight: invisible, and hunger runs at x2/3 (mobs.c:69-83) | MISSING | MED |
| Dew vial | Drinking heals PCT[v]% of HT: 5, 13, 21, 31, 41, 52, 63, 75, 88, 100. **At 10 it auto-revives on death** (use.c:355-361; G:505-508) | PARTIAL (drinks at 3 or 5, DB:708, 774) | HIGH |
| Dewdrops | Come from trampled high grass (G:1118-1123) and evil-eye loot. When HP<HT a drop **heals 1+(d-1)/5** instead of filling the vial; the vial fills only at full HP (hero_impl.h:73-82) | MISSING | MED-HIGH |
| Ankh | Revives at full HP with hunger 0, exp 0, gold 0. Keeps equipped gear and unique items (vial, quest items); **the whole backpack and the bags are lost**. The floor is reset (floor.c:555-590) | PARTIAL (buys and keeps; no risk change) | LOW |
| XP | A kill gives exp only if hero lvl ≤ mob max_lvl (G:541). Level L→L+1 needs 5+5L (G:158). Level-up: +5 HT, +5 HP, +1 accuracy, +1 defense (G:517-525) | MISSING (P_HUNT=0; no max_lvl filter) | HIGH |
| Respawn | 1 mob every 50 turns (25 at night or with the Amulet) if alive < 2+d%5+rnd(3) (NPCs count). Spawns WANDERING, out of sight. **Immediate on every arrival** (G:942-951, 962; floor.c:523, 539). None on boss floors, d21, d26 | PARTIAL (used only by the quest bounce) | HIGH |
| Initial mobs | 2+d%5+rnd(3), all asleep (content.c:342-347) | HANDLED | — |
| Waking | Asleep: wakes with chance 1/(dist+stealth) per mob act while it sees the hero; **at distance 1 it always wakes**. Wandering: notices with chance 1/(dist/2+shadows), certain within 3. Stealth comes from Ring of Shadows +L and levitation +2 (G:731-748) | **BUG in the model** (DB:1306) | MED |
| Surprise | A mob that has not noticed you (not MF_SEEN) has 0 defense, so you always hit; an Assassin adds up to +100% damage (G:1006, 1034) | PARTIAL (no ranged opener on sleepers) | MED |
| Mob sight | Range 8 (4 for succubus/eye/scorpio) **whatever the darkness** (mobs.c:20-35) | HANDLED (s_ambush, amb_retreat) | — |
| View distance | 8, caves 6, d22-24: 3/2/1, d25: 3. A torch gives 4 for 250 turns (G:327-331, use.c:461) | HANDLED (DB:1771) | — |
| Speed and time | Move costs 1.3^encumbrance; Freerunner x5/8; cripple x2. Haste ring 1.1^-L; Slow x2 on everything; attack delay is the weapon's DLY x1.2^encumbrance (G:199-216) | PARTIAL (armor speed in gear_cost; no Slow or cripple) | LOW-MED |
| Poison | Deals left/3+1 each turn, then left-1 (G:583-590). **Total for p = Σ(k/3+1)**: p=8 gives 17, p=12 gives 34 | MISSING (no "remaining damage" check) | MED |
| Bleeding | Level drops to about 3/4 each turn and deals that (mobs.c:321-324); total about 3x the start | MISSING (minor) | LOW |
| Burning | 1-5 per turn for up to 8 turns. Water puts it out; grass relights it (mobs.c:294-316) | HANDLED (DB:1812-1822, danger 2138) | — |
| Paralysis / frost | Cannot act; defense halved; damage can break paralysis | HANDLED (waits, DB:1790) | — |
| Roots | Cannot move | HANDLED (DB:1811, 1836, 1439) | — |
| Blindness | No scrolls; sight is lost | HANDLED (blind checks) | — |
| Charm | You cannot melee the charmer; it can still hit you (G:1250) | PARTIAL (rests, DB:1430; could throw or zap) | LOW |
| Weakness | STR-2 for 40 turns from city warlocks (G:167; city.c:28) | **BUG** (gear and junk judged with hero_str) | MED (city) |
| Vertigo | Every step is in a random direction, **including onto traps and chasms** (G:1155-1157) | MISSING | LOW-MED |
| Slow / cripple | Every action costs x2 / moves cost x2 (meat, falls, traps) | MISSING | LOW |
| Ooze | 1 damage per turn until you stand in water | HANDLED (Goo code) | — |
| Levitation / invisibility / mind vision | Puzzles, escape, stealth | HANDLED | — |
| Barkskin, Earthroot | DR bonus | HANDLED (Earthroot at Goo) | LOW |
| Fury / combo / sniper's mark / Warlock | Subclass effects. Warlock heals and feeds 10 per kill; Freerunner moves 1.6x faster | MISSING (always picks way 0: Gladiator, Battlemage, Assassin, Sniper) | LOW (few games reach Tengu) |
| Challenges | None in this port | N/A | — |

## 2. Top gaps

### G1. Sungrass is planted and never triggered (BUG, HIGH)
DB:701 plants a sungrass seed on the hero's own cell, then goes on resting or exploring. Planting does not press the cell (trap.c:140-155). The Health effect starts only when a character **steps onto** the plant, and lasts only while it stays there (G:954-957). The result is a wasted seed, or worse, a plant left for a monster to heal on (trap.c:114-117). The effect is HT/10 every 5 turns, 0.02·HT per turn: a full heal in about 50 turns, against about 10·HT turns of resting.

**Fix:** keep `s_sun_cell`. After planting, step to a free neighbour (one turn). The next decision taps back onto `s_sun_cell`, then rests while `G.health_pos == h->pos && HP < HT` and no enemy is within 3. Also treat a seen sungrass plant (`G.plants[i].type == 4`, which gardens make) as a rest target when hurt and within about 15 steps. Avoid walking over a sungrass plant otherwise.

### G2. XP: the bot arrives under-levelled (HIGH)
The lab (project doc) shows Goo win rates of 6% at level 3, 18% at level 4 and 36% at level 5. The bot arrives at levels 3-5; the zone-kit's typical level after Goo is 6-7. The numbers:
- Cumulative exp for level L is 5(L-1)(L+2)/2: level 5 needs 70, level 6 100, level 10 270, level 15 595, level 20 1045.
- Sewer starting mobs (d1-d4, about 22 sleepers at 1-2.3 exp) give about 40 exp, which is level 3.75. The rest has to come from **respawns**: one per 50 turns, and also one on every arrival.
- Exp stops when level > max_lvl: rats at 5, gnolls at 8, crabs at 9, skeletons and thieves at 10, shamans at 14, caves at 15-16, city at 20-22, halls at 25. Prison mobs are worth nothing past level 10 except shamans.
- P_HUNT=0 (DB:72), so wandering respawns the bot can see are left alone (DB:1138), and the 25% of turns spent resting (where respawns happen) do not turn into kills.

**Fix:**
- Set P_HUNT to about 60, and in `plan_next` consider only mobs with `h->lvl <= MOB_DEFS[t].max_lvl && exp > 0` that `losing_fight()` says it would win.
- In `fight()`, a non-hunting mob that gives no exp is ignored when not in the way (`ignore_mob`).
- When resting, prefer a cell with several exits in a central room on the respawn path over a dead end. The stair rest is fine; wanderers path to random room cells.
- Before a boss floor (`boss_depth(G.depth+1)`): if level < (depth+1) (level 5 at d4, 10-11 at d9...) and the food budget (G4) is above 150, stay and rest or wander until level up or the budget is gone, killing what comes.

### G3. Dew is wasted, and the full-vial revive is ignored (HIGH)
A vial at 10 is an extra life: on death HP += HT (G:505). Drinking at 5 gives 41%, and at 3 gives 21% (use.c:356). Dewdrops picked up while HP<HT heal only 1+(d-1)/5 each (1 HP in the sewers) instead of filling the vial. The bot rests to 80%, never 100%, so its vial almost never fills: decide() picks up whatever is underfoot (DB:1826), including the drop that trampling puts under the hero.

**Fix:**
- (a) With a vial, `G.dew<10` and `HP<HT`, skip IK_DEW heaps: the underfoot rule and heap_wanted both get a `!(vial && dew<10 && HP<HT && HP>HT/3)` guard. Remember the cells in a per-floor list.
- (b) When HP==HT (after a rest, or with Mending), the plan sweeps the remembered drops within about 12 steps.
- (c) `emergency()` (DB:774) skips the vial when `G.dew==10`: auto-drink does the same at death, with no waste if you survive. `use_items_when_safe` (DB:708) drinks only if dew<10 **and** the food budget is low; otherwise it rests.

### G4. No food budget; resting is priced as free (HIGH)
Resting is about 25% of all turns. Each HP regenerated costs 10 hunger: a ration is worth about 26 HP. Supply is about 280 per non-boss floor plus the start (360 grace + 260), minus 36 per stair use. That is about 250 turns per floor in total. About 60% of Goo deaths happen while starving (project doc). `lean` (DB:1844) switches only on "no food in the bag".

**Fix:** compute `budget = (360-hunger) + Σ energy(food in bag) + (food_not_yet_found_here ? 280 : 0) - 36`. Compare it with `need = turns left to explore (frontier estimate) + (boss_depth(d+1) ? 300 : 0)`. Pick rest thresholds from budget-need:
- surplus > 200: 50/80 as now;
- 0 to 200: 40/65;
- below 0: rest only to 35%, and use dew/sungrass/healing first.

The quest bounce (DB:1956-1961) and evidence searches should charge against the budget (72 per bounce), not stop only at "hungry" (DB:1954, which ignores what is in the bag). The garden (G6) and searching while resting (G5) make rest cheaper.

### G5. Search instead of rest (MED, cheap)
A search passes 2 turns with the same regeneration and hunger as 2 rests, and finds every secret within 1 for certain (G:1098). The bot spends up to 60 searches a floor on `evidence_hidden` (DB:1969), which is extra food on top of the resting.

**Fix:** in the rest branch (DB:1852), if any neighbour is a wall cell not yet searched (`!BIT_GET(g_srchd,h->pos)` / `s_searched`) and `wall_worth()` is above 0, call `bot_search()` instead of `game_rest(false)`. Better still, choose the rest spot with `search_spot()` when it is within 6 steps.

### G6. Rest in a garden (MED)
In an RT_GARDEN room with no enemy in sight, the hero is invisible (mobs cannot notice you) and hunger runs at x2/3 (mobs.c:69-83). Gardens can also hold sungrass bushes.

**Fix:** when the rest rule fires and a visited garden room is within about 15 steps and no foe is near, walk there first. Rest there instead of on the stairs, except on floors where crabs or bats are in sight.

### G7. "Floor food not yet found" (MED-HIGH, simple)
`famished` "hungry: down" (DB:1881-1884) leaves the floor when no *known* food heap remains. But each non-boss floor except d21 holds one FOOD item in a standard room (content.c:14, 336). A human player knows this.

**Fix:** set `s_floor_food_seen` when any IK_RATION/IK_PASTY heap is first seen on this floor (or one is picked up). While it is false, the floor is not a boss floor, and `floor_left()` is true, keep exploring instead of "hungry: down", and give unvisited standard rooms a small bonus in `plan_next`.

### G8. Poison and bleeding: no remaining-damage check (MED)
Poison p deals Σ_{k=1..p}(k/3+1): p=8 (spinner) gives about 17, p=12 gives 34, and mystery meat gives HT/5. Neither rest nor walking stops it. A healing potion (use.c:70) or carpaccio cures it.

**Fix:** in `emergency()`, before the HP% test, compute `dot = Σ(k/3+1, k=1..poison) + 3*bleed + (burning? 3*burning : 0)`. If `dot >= HP` and a known healing potion is held, drink it. Also block `prep_retreat` and stair walks that are pointless. A small helper.

### G9. The sleeper "sure first hit" is wrong (BUG in the model, MED)
DB:1306 attacks sleepers within 2 because "asleep, it can't dodge". Actors run tick-synchronously, so the sleeper always gets one act between the hero stepping adjacent and the hero's blow. At distance 1, `rnd_int(1+stealth)==0` always wakes it with stealth 0, which sets MF_SEEN and full defense (G:731-738). A sure hit only exists for a **ranged** opener (darts or a wand from 2 or more cells; def 0, G:1006), or when levitating or wearing a Ring of Shadows.

**Fix:** for sleeping or unaware non-boss mobs in line of fire, call `ranged_try()` with a cheap missile first (Assassin: always). Keep the melee approach only for the "it would wake anyway" reason.

### G10. Weakness changes gear choices (BUG, MED in the city)
The gear model (DB:641-646) and junk dropping (DB:709-718) use `hero_str()`, which takes 2 STR off while weakened (G:167). Weakness from a warlock lasts 40 turns (city.c:28). A weakened hero can swap armour (2x equip time) and then **permanently drop** a heavier spare as "junk".

**Fix:** skip the gear and junk blocks while `h->weakness > 0`, or evaluate with `G.hero.STR`.

## 3. Other model errors and notes

- **Eat comment (DB:1776):** "a ration eaten early wastes up to 100 turns" is wrong for rations. Eating at hunger h wastes max(0, E-h): a ration at 260 or more wastes nothing. Only a pasty (360) needs to wait for starving. The policy is harmless. A small gain: a warrior (+5 HP) or a mage (+1 charge per wand) should eat a ration as soon as hungry (≥260) when hurt or wands are low.
- **Stair rest (DB:1849-1851):** walking up to 20 steps to the entrance and back costs about 40 turns, and fleeing upstairs costs 72 hunger for the round trip. Fine, but include it in G4's budget.
- **Duel maths (`losing_fight`, DB:786-813):** ignores hero Slow (x2 enemy actions), the Ring of Evasion (1.2^L) and a rogue's light-armour dodge in `hero_def_est`. It underestimates danger while slowed. Add `if (h->b.slow) in *= 2`.
- **Vertigo:** a confused hero's steps go to random cells, traps and chasms included. `escape_air` walks out of confusion gas regardless. Suggested rule: with `h->vertigo` and a known trap or chasm adjacent, and no toxic gas or fire on the cell, rest instead of moving.
- **Charm (DB:1430):** rests beside the charmer. Throwing or zapping at it is allowed (the block is melee only, G:1250); `ranged_try(best)` first.
- **Ankh:** the backpack and bags are lost on revival. When an Ankh is held and death is likely, the bot should spend its consumables (unknown potions and scrolls, wands) earlier, since they are lost anyway.
- **Level cap:** the port has none (Java caps at 30). Harmless.
- **Head-start runs (HS=3):** a free level on every floor (DB:2436) plus Mending and Satiety rings hide G2 and G4. Tune these rules with HS=0.
