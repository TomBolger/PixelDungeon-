"""Every creature of Pixel Dungeon in one table: stats, flags, loot, sprite (0.8).

  python3 tools/mobs.py text   resources/text/mobs.txt (names, descriptions) - before build_text.py
  python3 tools/mobs.py bin    resources/data/mobs.bin (MobDef[MT_COUNT]) + src/c/mobs.auto.h - after build_text.py
  python3 tools/mobs.py art    images/mob_<key>.png (frames used by the animations, compacted, outlined, mirrored),
                               resources/data/mob_art.bin (CharArt[MT_COUNT]), package.json bitmap entries

Stats come from actors/mobs/*.java (HT, attackSkill, defenseSkill, damageRoll, dr, EXP, maxLvl, speed,
attackDelay); descriptions are read from the Java at generation time. The game loads MobDef[] once and
each CharArt with its sheet, so nothing here costs app image space.
"""
import json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paths import PD_SRC, PD_ASSETS  # upstream Pixel Dungeon (third_party submodule)
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PD = PD_SRC
ASSETS = PD_ASSETS
sys.path.insert(0, os.path.dirname(__file__))

# flags (keep in sync with pd.h MD_*): see the enum written into mobs.auto.h
FLAGS = ["FLY", "NPC", "BOSS", "I_BURN", "I_GAS", "I_PARA", "I_POISON", "I_TERROR", "I_AMOK", "I_SLEEP", "I_CHARM",
         "I_ROOTS", "I_FROST", "I_DEATH", "I_PSI", "I_LEECH", "I_VERTIGO", "I_FIREBOLT", "R_GAS", "R_DEATH", "R_PSI",
         "R_DISINT", "R_LEECH", "R_POISON", "WATER", "PASSIVE", "IMMOVABLE", "R_SHOCK"]
LOOT = ["NONE", "GOLD", "MEAT", "HEALING", "FLAME", "DEW", "FOOD", "SCROLL", "POTION", "LULLABY", "HAGGLER", "THORNS",
        "BEACON", "WEAPON"]
VERBS = ["dodged", "parried", "blocked", "evaded", "absorbed"]
TICK = 60

def M(key, java, name, sprite, HT=1, att=0, df=0, dmg=(0, 0), dr=0, exp=0, maxlvl=30, speed=1.0, adelay=1.0,
      verb="dodged", flags=(), loot="NONE", loot_pct=0, desc_cls=None, desc=None):
    return dict(key=key, java=java, name=name, sprite=sprite, HT=HT, att=att, df=df, dmg=dmg, dr=dr, exp=exp, maxlvl=maxlvl,
                speed=speed, adelay=adelay, verb=verb, flags=flags, loot=loot, loot_pct=loot_pct, desc_cls=desc_cls, desc=desc)

# order = MobType enum. The first five keep their 0.7 numbers (saves store the type byte).
MOBS = [
    M("RAT", "Rat", "marsupial rat", "RatSprite", 8, 8, 3, (1, 5), 1, 1, 5),
    M("GNOLL", "Gnoll", "gnoll scout", "GnollSprite", 12, 11, 4, (2, 5), 2, 2, 8, loot="GOLD", loot_pct=50),
    M("CRAB", "Crab", "sewer crab", "CrabSprite", 15, 12, 5, (3, 6), 4, 3, 9, speed=2, verb="parried", loot="MEAT", loot_pct=17),
    M("MIRROR", "npcs/MirrorImage", "mirror image", None, 1, 0, 0, (0, 0), 0, 0, 0, flags=("I_GAS", "I_BURN")),
    M("SHEEP", None, "sheep", "SheepSprite", 1, desc="This is a magic sheep. What's so magical about it? You can't kill it. It will stand there until it magcially fades away, all the while chewing cud with a blank stare."),
    # sewers
    M("ALBINO", "Albino", "albino rat", "AlbinoSprite", 15, 8, 3, (1, 5), 1, 1, 5, desc_cls="Rat"),
    M("SWARM", "Swarm", "swarm of flies", "SwarmSprite", 80, 12, 5, (1, 4), 0, 1, 10, verb="evaded", flags=("FLY",)),
    M("FETID_RAT", "FetidRat", "fetid rat", "RatSprite", 15, 12, 5, (2, 6), 2, 3, 5, verb="evaded", flags=("I_PARA",)),
    M("GHOST", "npcs/Ghost", "sad ghost", "GhostSprite", 1, 0, 0, speed=0.5, verb="evaded", flags=("FLY", "NPC", "I_PARA", "I_ROOTS")),
    M("GOO", "Goo", "Goo", "GooSprite", 80, 15, 12, (2, 12), 2, 10, 30, flags=("BOSS", "R_GAS", "R_DEATH", "R_PSI"), loot="BEACON", loot_pct=33),
    M("RAT_KING", "npcs/RatKing", "rat king", "RatKingSprite", 1, 0, 0, speed=2, flags=("NPC",)),
    # prison
    M("SKELETON", "Skeleton", "skeleton", "SkeletonSprite", 25, 12, 9, (3, 8), 5, 5, 10, verb="blocked", flags=("I_DEATH",), loot="WEAPON", loot_pct=20),
    M("THIEF", "Thief", "crazy thief", "ThiefSprite", 20, 12, 12, (1, 7), 3, 5, 10, adelay=0.5, loot="HAGGLER", loot_pct=1),
    M("BANDIT", "Bandit", "crazy bandit", "BanditSprite", 20, 12, 12, (1, 7), 3, 5, 10, adelay=0.5, loot="HAGGLER", loot_pct=1, desc_cls="Thief"),
    M("SHAMAN", "Shaman", "gnoll shaman", "ShamanSprite", 18, 11, 8, (2, 6), 4, 6, 14, flags=("R_SHOCK",), loot="SCROLL", loot_pct=33),
    M("WANDMAKER", "npcs/Wandmaker", "old wandmaker", "WandmakerSprite", 1, verb="absorbed", flags=("NPC",)),
    M("TENGU", "Tengu", "Tengu", "TenguSprite", 120, 20, 20, (8, 15), 5, 20, 30, flags=("BOSS", "R_GAS", "R_POISON", "R_DEATH", "R_PSI")),
    # caves
    M("BAT", "Bat", "vampire bat", "BatSprite", 30, 16, 15, (6, 12), 4, 7, 15, speed=2, verb="evaded", flags=("FLY", "R_LEECH"), loot="HEALING", loot_pct=13),
    M("BRUTE", "Brute", "gnoll brute", "BruteSprite", 40, 20, 15, (10, 40), 8, 8, 15, flags=("I_TERROR",), loot="GOLD", loot_pct=50),
    M("SHIELDED", "Shielded", "shielded brute", "ShieldedSprite", 40, 20, 20, (10, 40), 10, 8, 15, verb="blocked", flags=("I_TERROR",), loot="GOLD", loot_pct=50, desc_cls="Brute"),
    M("SPINNER", "Spinner", "cave spinner", "SpinnerSprite", 50, 20, 14, (12, 16), 6, 9, 16, flags=("R_POISON", "I_ROOTS"), loot="MEAT", loot_pct=13),
    M("BLACKSMITH", "npcs/Blacksmith", "troll blacksmith", "BlacksmithSprite", 1, flags=("NPC",)),
    M("DM300", "DM300", "DM-300", "DM300Sprite", 200, 28, 18, (18, 24), 10, 30, 30, flags=("BOSS", "R_DEATH", "R_PSI", "I_GAS"), loot="THORNS", loot_pct=33),
    # city
    M("ELEMENTAL", "Elemental", "fire elemental", "ElementalSprite", 65, 25, 20, (16, 20), 5, 10, 20, flags=("FLY", "I_BURN", "I_FIREBOLT", "I_PSI"), loot="FLAME", loot_pct=10),
    M("WARLOCK", "Warlock", "dwarf warlock", "WarlockSprite", 70, 25, 18, (12, 20), 8, 11, 21, flags=("R_DEATH",), loot="POTION", loot_pct=83),
    M("MONK", "Monk", "dwarf monk", "MonkSprite", 70, 30, 30, (12, 16), 2, 11, 21, adelay=0.5, verb="parried", flags=("I_AMOK", "I_TERROR"), loot="FOOD", loot_pct=8),
    M("SENIOR", "Senior", "senior monk", "SeniorSprite", 70, 30, 30, (12, 20), 2, 11, 21, adelay=0.5, verb="parried", flags=("I_AMOK", "I_TERROR"), loot="FOOD", loot_pct=8, desc_cls="Monk"),
    M("GOLEM", "Golem", "golem", "GolemSprite", 85, 28, 18, (20, 40), 12, 12, 22, adelay=1.5, verb="blocked", flags=("R_PSI", "I_AMOK", "I_TERROR", "I_SLEEP")),
    M("IMP", "npcs/Imp", "ambitious imp", "ImpSprite", 1, verb="evaded", flags=("NPC",)),
    M("KING", "King", "King of Dwarves", "KingSprite", 300, 32, 25, (20, 38), 14, 40, 30, verb="parried", flags=("BOSS", "R_GAS", "R_DEATH", "R_PSI", "R_DISINT", "I_PARA", "I_VERTIGO")),
    M("UNDEAD", "King", "undead dwarf", "UndeadSprite", 28, 16, 15, (12, 16), 5, 0, 30, verb="blocked", flags=("I_DEATH", "I_PARA"), desc_cls="Undead"),
    # halls
    M("SUCCUBUS", "Succubus", "succubus", "SuccubusSprite", 80, 40, 25, (15, 25), 10, 12, 25, flags=("R_LEECH", "I_SLEEP"), loot="LULLABY", loot_pct=5),
    M("EYE", "Eye", "evil eye", "EyeSprite", 100, 30, 20, (14, 20), 10, 13, 25, adelay=1.6, flags=("FLY", "R_DISINT", "R_DEATH", "R_LEECH", "I_TERROR"), loot="DEW", loot_pct=50),
    M("SCORPIO", "Scorpio", "scorpio", "ScorpioSprite", 95, 36, 24, (20, 32), 16, 14, 25, flags=("R_LEECH", "R_POISON"), loot="HEALING", loot_pct=13),
    M("ACIDIC", "Acidic", "acidic scorpio", "AcidicSprite", 95, 36, 24, (20, 32), 16, 14, 25, flags=("R_LEECH", "R_POISON"), loot="HEALING", loot_pct=13, desc_cls="Scorpio"),
    M("YOG", "Yog", "Yog-Dzewa", "YogSprite", 300, 0, 0, (0, 0), 0, 50, 30,
      flags=("BOSS", "IMMOVABLE", "I_DEATH", "I_TERROR", "I_AMOK", "I_CHARM", "I_SLEEP", "I_BURN", "I_GAS", "I_PSI"), desc="YOG"),
    M("ROTTING_FIST", "Yog", "rotting fist", "RottingFistSprite", 300, 36, 25, (24, 36), 15, 0, 30,
      flags=("R_GAS", "R_DEATH", "R_PSI", "I_AMOK", "I_SLEEP", "I_TERROR", "I_POISON", "I_VERTIGO"), desc="YOG"),
    M("BURNING_FIST", "Yog", "burning fist", "BurningFistSprite", 200, 36, 25, (20, 32), 15, 0, 30,
      flags=("R_GAS", "R_DEATH", "R_PSI", "I_AMOK", "I_SLEEP", "I_TERROR", "I_BURN"), desc="YOG"),
    M("LARVA", "Yog", "god's larva", "LarvaSprite", 25, 30, 20, (15, 20), 8, 0, 30, desc="YOG"),
    # anywhere
    M("SHOPKEEPER", "npcs/Shopkeeper", "shopkeeper", "ShopkeeperSprite", 1, flags=("NPC",)),
    M("IMP_SHOP", "npcs/ImpShopkeeper", "ambitious imp", "ImpSprite", 1, verb="evaded", flags=("NPC",)),
    M("MIMIC", "Mimic", "mimic", "MimicSprite", 10, 9, 5, (1, 5), 1, 2, 30, flags=("I_PSI",)),
    M("WRAITH", "Wraith", "wraith", "WraithSprite", 1, 10, 50, (1, 3), 0, 0, 30, verb="evaded", flags=("FLY", "I_DEATH", "I_TERROR")),
    M("STATUE", "Statue", "animated statue", "StatueSprite", 15, 10, 4, (1, 5), 0, 0, 30, flags=("R_GAS", "R_POISON", "R_DEATH", "R_PSI", "I_LEECH", "PASSIVE")),
    M("PIRANHA", "Piranha", "giant piranha", "PiranhaSprite", 10, 20, 10, (1, 5), 0, 0, 30, speed=2, flags=("WATER", "I_BURN", "I_PARA", "I_GAS", "I_ROOTS", "I_FROST")),
    M("BEE", "npcs/Bee", "golden bee", "BeeSprite", 1, 0, 9, flags=("FLY", "I_POISON")),
]

YOG_DESC = None
def java_desc(m):
    if m["desc"] and m["desc"] != "YOG": return m["desc"]
    if not m["java"]: return ""
    f = os.path.join(PD, "actors/mobs", m["java"].split("/")[0] + ".java") if "/" not in m["java"] else os.path.join(PD, "actors/mobs", m["java"] + ".java")
    s = open(f).read()
    if m["desc"] == "YOG":
        body = re.search(r'TXT_DESC\s*=(.*?);', s, re.S).group(1)
        return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', body)).replace('\\"', '"')
    cls = m["desc_cls"] or os.path.basename(m["java"])
    if m["desc_cls"] and not re.search(r'class %s\b' % cls, s):
        s = open(os.path.join(PD, "actors/mobs", cls + ".java")).read()
    start = re.search(r'class %s\b' % cls, s).start()
    mm = re.search(r'public String description\(\)\s*\{(.*?)\n\t*\}', s[start:], re.S)
    if not mm: return ""
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', mm.group(1))).replace('\\"', '"')

def write_text():
    lines = ["# generated by tools/mobs.py - creature names and descriptions"]
    for m in MOBS:
        lines.append(f"MOBN_{m['key']}\t{m['name']}")
        lines.append(f"MOBD_{m['key']}\t{java_desc(m)}")
    new = "\n".join(lines) + "\n"
    p = os.path.join(ROOT, "resources/text/mobs.txt")
    if not os.path.exists(p) or open(p).read() != new: open(p, "w").write(new)

def write_bin():
    ids = {}
    for line in open(os.path.join(ROOT, "src/c/text_ids.h")):
        p = line.split()
        if len(p) == 3 and p[0] == "#define": ids[p[1]] = int(p[2])
    out = b""
    for m in MOBS:
        fl = 0
        for f in m["flags"]: fl |= 1 << FLAGS.index(f)
        move = round(TICK / m["speed"]); ad = round(TICK * m["adelay"])
        out += struct.pack("<IHHH12B2x", fl, ids["TX_MOBN_" + m["key"]], ids["TX_MOBD_" + m["key"]], m["HT"], m["att"], m["df"],
                           m["dmg"][0], m["dmg"][1], m["dr"], m["exp"], m["maxlvl"], move, ad, VERBS.index(m["verb"]),
                           LOOT.index(m["loot"]), m["loot_pct"])
    open(os.path.join(ROOT, "resources/data/mobs.bin"), "wb").write(out)
    h = ["// generated by tools/mobs.py - do not edit", "#pragma once",
         "typedef enum { " + ", ".join("MT_" + m["key"] for m in MOBS) + ", MT_COUNT } MobType;",
         "enum { " + ", ".join(f"MD_{f} = 1u << {i}" for i, f in enumerate(FLAGS)) + " };",
         "enum { " + ", ".join("LOOT_" + l for l in LOOT) + " };",
         f"#define MOB_VERBS {{ " + ", ".join(f'"{v}"' for v in VERBS) + " }"]
    new = "\n".join(h) + "\n"
    p = os.path.join(ROOT, "src/c/mobs.auto.h")
    if not os.path.exists(p) or open(p).read() != new: open(p, "w").write(new)
    print(f"mobs.bin: {len(MOBS)} creatures, {len(out)} B")

# ---- art ----
LIFT = {"GOO": (2, 40), "WRAITH": (1, 70)}             # per-pixel v * k + a before snapping
ANIMS = ["idle", "run", "attack", "die", "zap"]           # A_IDLE, A_RUN, A_ATTACK, A_DIE, A_OPERATE (ranged / special)
SEQ = 10
def write_art():
    from parse_sprites import parse
    import convert_lib as cl
    from PIL import Image
    pkg = json.load(open(os.path.join(ROOT, "package.json")))
    media = pkg["pebble"]["resources"]["media"]
    arts = b""
    for m in MOBS:
        sp = m["sprite"]
        if not sp:                                        # the mirror image wears the hero's sheet (main.c HERO_ART)
            arts += bytes(2 + 6 * SEQ + 12); continue
        p = parse(sp)
        if p["texture"] is None and p["extends"] != "MobSprite": p2 = parse(p["extends"]); p2["anims"].update(p["anims"]); p = p2
        an = p["anims"]
        alias = {"zap": ["zap", "cast", "kick", "pump"]}
        seqs = []
        for a in ANIMS:
            cand = alias.get(a, [a])
            got = next((an[c] for c in cand if c in an and an[c][2]), None)
            if not got and a in ("run", "attack"): got = an.get("idle")
            if not got and a == "zap": got = an.get("attack") or an.get("idle")
            seqs.append(got)
        used = []
        for s in seqs:
            for f in (s[2] if s else []):
                if f not in used: used.append(f)
        tex = Image.open(os.path.join(ASSETS, p["texture"])).convert("RGBA")
        fw, fh = p["fw"], p["fh"]
        cols = tex.width // fw
        strip = Image.new("RGBA", (fw * len(used), fh))
        for i, f in enumerate(used):
            strip.paste(tex.crop(((f % cols) * fw, (f // cols) * fh, (f % cols) * fw + fw, (f // cols) * fh + fh)), (i * fw, 0))
        if m["key"] in LIFT:                              # very dark sprites (Goo, wraiths) would vanish into black
            k, a = LIFT[m["key"]]; px = strip.load()
            for y in range(strip.height):
                for x in range(strip.width):
                    r, g, b, al = px[x, y]
                    if al: px[x, y] = (min(255, r * k + a), min(255, g * k + a), min(255, b * k + a), 255)
        if m["key"] == "PIRANHA":                         # its idle frames are a 30% black shadow under the water: solid grey here
            px = strip.load()
            for y in range(strip.height):
                for x in range(strip.width):
                    if 0 < px[x, y][3] < 128: px[x, y] = (85, 85, 85, 255)
        img = cl.outlined(cl.max16(cl.pebbleize(strip, cl.CHARS)), fw)   # 0.20: no mirrored row, the blitter flips
        name = "mob_" + m["key"].lower() + ".png"
        img.save(os.path.join(ROOT, "resources/images", name))
        rec = struct.pack("<BB", fw, fh)
        lens, fpss, body = [], [], b""
        for s in seqs:
            fr = [used.index(f) for f in s[2]] if s else [0]
            fps = s[0] if s else 1
            if len(fr) > SEQ:                              # long idles: every k-th frame, k times slower
                k = (len(fr) + SEQ - 1) // SEQ; fr = fr[::k]; fps = max(1, round(fps / k))
            lens.append(len(fr)); fpss.append(fps)
            body += bytes(fr + [0] * (SEQ - len(fr)))
        body += bytes(SEQ)                                 # A_READ: heroes only
        lens += [1]; fpss += [1]
        rec += body + bytes(lens) + bytes(fpss)
        arts += rec
        rname = "IMAGE_MOB_" + m["key"]
        if not any(e["name"] == rname for e in media):
            media.append({"type": "bitmap", "name": rname, "file": "images/" + name, "memoryFormat": "Smallest",
                          "spaceOptimization": "memory", "targetPlatforms": ["emery", "gabbro"]})
    open(os.path.join(ROOT, "resources/data/mob_art.bin"), "wb").write(arts)
    if not any(e["name"] == "MOB_ART" for e in media):
        media.append({"type": "raw", "name": "MOB_ART", "file": "data/mob_art.bin", "targetPlatforms": ["emery", "gabbro"]})
    if not any(e["name"] == "MOB_DEFS" for e in media):
        media.append({"type": "raw", "name": "MOB_DEFS", "file": "data/mobs.bin", "targetPlatforms": ["emery", "gabbro"]})
    json.dump(pkg, open(os.path.join(ROOT, "package.json"), "w"), indent=2); open(os.path.join(ROOT, "package.json"), "a").write("\n")
    res = ["// generated by tools/mobs.py - each creature's sheet (0 = none: the mirror image uses the hero's)", "#pragma once",
           "static const uint32_t MOB_RES[MT_COUNT] = { " + ", ".join(("RESOURCE_ID_IMAGE_MOB_" + m["key"]) if m["sprite"] else "0" for m in MOBS) + " };"]
    open(os.path.join(ROOT, "src/c/mob_res.auto.h"), "w").write("\n".join(res) + "\n")
    print(f"mob_art.bin: {len(arts)} B")

if __name__ == "__main__":
    {"text": write_text, "bin": write_bin, "art": write_art}[sys.argv[1]]()
