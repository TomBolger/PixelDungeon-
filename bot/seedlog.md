# Seed-solve log (all runs FAIR=1 HS=0: look-ahead plays a scrubbed copy; plain look-ahead peeks at unseen floors/monsters and re-seeds dice)
Seed: game 98 warrior (CLS=0). Honest scan of games 0-149: best warriors 98 and 106 (both d15 in a batch run). 84/1508/1362 die at d3-4 honestly.
Baseline (single run): died d12, turn 3645, gnoll brute. Starving from d1 (no spare food all game), hero lvl 10 with a spear-1 and STR 13.
1. Pickaxe (blacksmith blood quest) was wielded as soon as it was picked up; the brute was fought with it. Rule: wield only with a vampire bat in view, put the real weapon back otherwise. Result: same death (brute, d12, t3648): the real spear is hardly better (1.8 dmg/turn vs brute armour 8). Kept (sound, neutral).
2. Hopeless duel (bot's own estimate: damage to take > 1.5x HP, no kit): retreat up stairs. Seed: fled, then a vampire bat killed it on d11. 24-game regression 4.58 vs 4.79 without: not helpful alone, left opt-in (HOPELESS=1).
Next: the real cause is a weak weapon (no weapon upgrades, STR 13) + hunger every floor; see report.

## Pass 2 (all runs FAIR=1 HS=0 D=1 RT=0 FK=0, warriors CLS=0, games 0-99 as the regression set; baseline mean depth 5.87)
0. Determinism: game 98 alone, inside START=90 and inside a 4-shard run of games 0-99 now give the identical line (was: games 2 and 11 changed game 98, through per-game statics in the bot). Fix: every game starts from the program's pristine variables (snapshot of the data segment at start, restored before each game; tallies kept). Commit 586aa2e. Baseline game 98 (old rule set): d12, turn 3648, gnoll brute.
1. Switch on the owner's PZTEST rule set by default (NOPZTEST=1 turns it off). Regression 5.87 -> 6.99 (100 games). Game 98: d15, turn 5067, DM-300. (PZTEST + PACE=2: 6.85 average, game 98 d20: not adopted yet.)
