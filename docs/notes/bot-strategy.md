# Pixel Dungeon 1.9.1 bot strategy (owner's rules + digested guides)

Sources: owner (Thomas); pixeldungeon.fandom.com (Wraith, Goo, Hunger, Special room, "11 Tips from a Veteran",
"Sewer Strategies!", "Little-Known Tactics and Tips"). The port's code is the ground truth for mechanics.

## Owner's rules
- Puzzle rooms come with a guaranteed item on the same floor: pool (piranhas) -> Invisibility, trap room -> Levitation,
  storage (barricade) -> Liquid Flame; locked rooms -> iron key; vault chests -> golden key. The bot may read room
  types from the game data (no need to "see" them).
- Graves / piranha pools: leave them until the floor is explored; then go to the room's door. Solver bot: test unknown
  potions at the pool doorway (not in water) until invisible -> take the prize; levitation -> float over traps.
- Graves: stand so walls/corners block most of the 4 spawn cells (orthogonal neighbours of the hero) before opening.
- Wraiths: 1 HP but huge dodge; melee rarely hits. Zap wands (never miss; weak to magic, poison, fire) or use the
  door trick (a monster following you through a door is hit for sure); lullaby -> surprise hit.
- If the floor is clear and the player heads for the stairs, skip the puzzle/graves.
- Emergency with no healing: plant a damaging seed (Sorrowmoss/Firebloom) between you and the enemy, step back.
- A full-health crab vs an 11 HP level-5 warrior on floor 3: fighting is acceptable. Hurt + no threat: rest, then explore.

## Guides (original PD)
- Upgrades into ARMOR (plate eventually); strength potions to wear heavier armor. Match hero level ~ depth.
- Identify: read unknown scrolls early (not floor 1: wastes mapping); drink-ID potions on floors 1-2 at full HP, away
  from grass, never while levitating; from floor 3 on only drink-test for a puzzle room. Floor 6 shop sells a
  guaranteed Healing: buying it IDs healing.
- Conserve healing potions: Sungrass, frozen meat, wells of health. Drink Healing at ~5-10% HP (sooner with enemies).
- Hunger: hungry at 260, starving at 360 (+1/turn per 10 turns); ration -260, pasty -360, meat -100. Stairs cost 10.
  Eat when hungry; don't over-rest/search.
- Doors: fight in doorways/corridors; a monster following through a door gets hit for sure (door trick).
- Don't melee piranhas (fight from land / range); don't eat mystery meat raw; don't walk around while poisoned.
- Gas trap rooms: trigger traps by throwing an item, wait for the gas; levitation crosses.
- Goo (80 HP, heals on water, pump-up "!!!" hits hard within 2 tiles): step out of range when it pumps; fight it away
  from water while you stand in water (cures ooze); ranged/wands while it approaches; save gas/flame potions for it.
- Dwarf King: kill burning fist, then rotting fist... (Yog); ignore minions where possible; save fire potions.
- Shops: buy unknown potions/scrolls early; sell spare weapons/armor/darts; don't buy weapons/armor/ankh.
