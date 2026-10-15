# Bot experiment log — 2026-10-14 (Pacific)

## Reproducible host simulations

This sandbox cannot resolve github.com over DNS, so the Linux host runs on GitHub Actions rather than inside the chat's container. The A/B harness is committed to `.github/workflows/bot-ab-sim.yml` (on this branch) and `bot/tools/ci_compare.py`. The workflow fetches the repository, compiles the actual watch-engine host simulator with `bot/build.sh`, then runs **the same seeded games** under control and treatment (`HS=0 RT=0 FK=0 D=1`, no `RESPAWN` or other oracle). Logs, a paired summary, and result artifacts are retained with each run.

Workflow: https://github.com/TomBolger/PixelDungeon-/actions/workflows/bot-ab-sim.yml

## Controlled results (initial 12 seeds, 0–11)

| Treatment | Actions run | Mean max depth | Paired delta vs 4.667 baseline | Pairs changed | Lookahead playouts | Assessment |
| --- | --- | ---: | ---: | --- | ---: | --- |
| Unmodified rules | #1 | 4.667 | — | — | 21,111 | reference |
| `EARLYLA=1` (early trigger, strict 2800 cutoff, 80 threshold) | [#1](https://github.com/TomBolger/PixelDungeon-/actions/runs/38667951593) | 4.083 | −0.583 ± 0.452 SE | 2 better, 3 worse | 35,514 | worse in this sample, cost +68% |
| `EARLYLA=1 EARLYLA_GATE_ONLY=1` (trigger only) | [#3](https://github.com/TomBolger/PixelDungeon-/actions/runs/38668430290) | 4.667 | +0.000 | 0 better, 0 worse | 21,135 | same depth on all 12: trigger alone adds negligible useful choice |
| `EARLYLA=1 LA_RESOURCE=1` (strict with conservation penalties) | [#5](https://github.com/TomBolger/PixelDungeon-/actions/runs/38668809563) | 4.417 | −0.250 ± 0.509 SE | 4 better, 4 worse | 33,483 | less negative, still no improvement |
| `THROWFIRST=1` (shoot earlier) | [#6](https://github.com/TomBolger/PixelDungeon-/actions/runs/38669072791) | 4.333 | −0.333 ± 0.333 SE | 0 better, 1 worse | 18,162 | no benefit in this sample |

These are **smoke experiments**: 12 seeds is not enough to make general win-rate claims. All arms had 0 wins. The repeated control reproduced exactly across Actions runs #1–#6, validating deterministic comparison on these inputs. No candidate here warrants merging based on evidence.

## What changed in our diagnosis

1. Activating lookahead earlier **without** changing the selection threshold did not change any of 12 run outcomes.
2. Lowering the selection threshold and demanding 80% post-fight score led to poor decisions and substantial extra compute. This points to rollout evaluation / candidate selection, not merely late triggering.
3. Penalizing finite consumable use improved some cases but did not outperform control overall. Resource tracking alone is not enough.
4. The existing `THROWFIRST` implementation did not help these 12 seeds; one died of starvation instead of reaching depth 8. Spending extra turns/ammo to avoid melee can have a global cost.

## Next discriminating test (not run yet)

Record the first divergent decision and full action context of at least seeds 1, 3, and 11: baseline versus aggressive lookahead, including HP, hunger, foes, candidates, chosen action, potion/scroll/food inventory deltas, and accumulated time. Use the existing STEPLOG/REC/STOP or debug harness in `deepbot.c`. Identify whether the primary bad decision comes from a false safe horizon, resource spending, path-dependent monster spawning, or lack of encounter-state memory. Then test **one isolated correction** on at least 50–100 paired seeds with a confidence interval; optimize survival *without* damaging XP/food budget.

The experimental code remains opt-in. Do not promote these settings to default based on the current outcomes.
