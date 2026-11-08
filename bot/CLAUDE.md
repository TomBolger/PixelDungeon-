# The bot: standing rules (Thomas, 2026-11-07)

Read this before touching bot/src. It overrides any habit or earlier plan.

## No new situation rules
Do not add any rule of the form "in situation X, do Y". Every change to how the bot plays is one of:
1. **A new option** the scorer can consider (a step, an attack, an item use, a destination on the map), or
2. **A better number** inside the scorer: damage and hit odds, death risk, item worth, what a turn of food costs, what XP is worth.

If a fix can't be expressed that way, stop and tell Thomas instead of adding a rule.

## Diagnosing a loss
The question is "which number was wrong, or which option was missing?" Never "what should it have done here?"

## Reporting
Every report says which number or option changed. Reporting a new rule counts as breaking this.
Progress is measured by the rule count going down as well as by results. A temporary dip in results is fine;
the bot is far from winning, so small gains from patches are worth nothing.

## Direction
One scorer decides every move: it lists everything the hero could do this turn, values each one by the game's
own math (the source is available: damage rolls, accuracy, regen, hunger, spawn odds, item odds), in one currency,
and picks the best. Old rules are deleted area by area as the scorer covers them.
