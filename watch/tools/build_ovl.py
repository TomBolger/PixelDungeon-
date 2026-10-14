"""Build the code overlays (src/ovl/<name>/*.c) for each platform against the app's final ELF.

  python3 tools/build_ovl.py            (after a `pebble build`; tools/build.sh runs the whole sequence)

Each overlay is compiled with -mlong-calls -fno-pie so every reference outside a function is an absolute
word (R_ARM_ABS32); it is linked at address 0 with the app's symbols imported (--just-symbols), and
the emitted relocations tell which words to move by the overlay's load address and which by the app's.
Output: resources/data/ovl_<name>~<platform>.bin (format in src/c/ovl.h).
"""
import ast, os, re, struct, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SDK = os.path.expanduser("~/.local/share/pebble-sdk/SDKs/current")
TC = os.path.join(SDK, "toolchain/arm-none-eabi/bin/arm-none-eabi-") if os.path.isdir(os.path.join(SDK, "toolchain")) else \
     os.path.expanduser("~/.local/share/pebble-sdk/SDKs/4.33.1/toolchain/arm-none-eabi/bin/arm-none-eabi-")
PLATFORMS = ["emery", "gabbro"]

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit(" ".join(cmd) + "\n" + r.stdout + r.stderr)
    return r.stdout

def cache(plat):
    env = {}
    for line in open(os.path.join(ROOT, "build/c4che", plat + "_cache.py")):
        k, _, v = line.partition(" = ")
        if _:
            try: env[k] = ast.literal_eval(v.strip())
            except Exception: pass
    return env

def symbols(elf, glob_only=False, own=False):
    syms = {}
    for line in run([TC + "readelf", "-sW", elf]).splitlines():
        p = line.split()
        if len(p) >= 8 and p[0].endswith(":") and p[1] != "Value" and p[6] != "UND" and (p[4] == "GLOBAL" or not glob_only) \
                and not (own and p[6] == "ABS" and not p[7].startswith("__ovl")):
            syms.setdefault(p[7], int(p[1], 16))
    return syms

def compile_objs(name, plat):
    env = cache(plat)
    out_dir = os.path.join(ROOT, "build/ovl", plat, name); os.makedirs(out_dir, exist_ok=True)
    srcs = sorted(os.path.join(ROOT, "src/ovl", name, f) for f in os.listdir(os.path.join(ROOT, "src/ovl", name)) if f.endswith(".c"))
    flags = ["-std=c99", "-mcpu=cortex-m3", "-mthumb", "-Os", "-ffunction-sections", "-fdata-sections", "-fno-common",
             "-mlong-calls", "-fno-optimize-sibling-calls", "-fno-pie", "-fno-pic", "-fno-exceptions", "-fno-asynchronous-unwind-tables",
             "-D_TIME_H_", "-Dtime_t=long", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-error=format-truncation",
             "-I", os.path.join(ROOT, "src/c"), "-I", os.path.join(ROOT, "build", plat), "-I", os.path.join(ROOT, "build/include"),
             "-I", os.path.join(SDK, "sdk-core/pebble", plat, "include")] + ["-D" + d for d in env["DEFINES"]]
    objs = []
    for s in srcs:
        o = os.path.join(out_dir, os.path.basename(s) + ".o"); run([TC + "gcc"] + flags + ["-c", s, "-o", o]); objs.append(o)
    return out_dir, objs

def gen_keep(plat=PLATFORMS[0]):
    """src/c/ovl_keep.auto.c: a table naming every app symbol an overlay uses, so --gc-sections keeps them."""
    need = set()
    for name in sorted(os.listdir(os.path.join(ROOT, "src/ovl"))):
        _, objs = compile_objs(name, plat)
        und, defined = set(), set()
        for o in objs:
            for line in run([TC + "nm", o]).splitlines():
                p = line.split()
                if len(p) == 2 and p[0] == "U": und.add(p[1])
                elif len(p) == 3 and p[1] in "TDRBC": defined.add(p[2])
        need |= und - defined - {"_GLOBAL_OFFSET_TABLE_"}
    names = sorted(need)
    if plat != PLATFORMS[0]: return need
    # 0.9: the app is linked with -Wl,-u,<symbol> for each (wscript reads keep_syms.txt): GC roots without a table in RAM
    new = "\n".join(names) + "\n"
    p = os.path.join(ROOT, "keep_syms.txt")
    old = open(p).read() if os.path.exists(p) else None
    if old != new: open(p, "w").write(new)
    stub = "// the overlays' app symbols are kept by linker flags now (see keep_syms.txt, wscript)\n"
    sp = os.path.join(ROOT, "src/c/ovl_keep.auto.c")
    if not os.path.exists(sp) or open(sp).read() != stub: open(sp, "w").write(stub)
    print(f"ovl keep: {len(names)} app symbols" + (" (changed)" if old != new else ""))
    changed = old != new
    for plat in PLATFORMS[1:]:                         # 0.35: symbols only another platform's overlays use (Gabbro's round windows)
        extra = "".join(s + "\n" for s in sorted(gen_keep(plat) - need))
        p = os.path.join(ROOT, f"keep_syms_{plat}.txt")
        old = open(p).read() if os.path.exists(p) else ""
        if old != extra: open(p, "w").write(extra); changed = True
        print(f"ovl keep {plat}: {len(extra.split())} more")
    return changed

def build(name, plat):
    out_dir, objs = compile_objs(name, plat)
    app_elf = os.path.join(ROOT, "build", plat, "pebble-app.elf")
    elf = os.path.join(out_dir, name + ".elf")
    run([TC + "ld", "-T", os.path.join(ROOT, "tools/ovl.ld"), "--just-symbols=" + app_elf, "-q", "--gc-sections",
         "--no-undefined", "-o", elf] + objs)
    app = symbols(app_elf, True)
    for o in objs:                      # a global the app also defines would silently bind to the app's copy
        for line in run([TC + "nm", o]).splitlines():
            q = line.split()
            if len(q) == 3 and q[1] in "TDRB" and q[2] in app and q[2] != "ovl_header":
                sys.exit(f"{name}/{plat}: {q[2]} is also defined by the app (make it static or rename it)")
    mine = symbols(elf, own=True)       # defined by the overlay itself (imported app symbols are ABS)
    img_end, end = mine["__ovl_img_end"], mine["__ovl_end"]
    img = run([TC + "objcopy", "-O", "binary", "-j", ".text", elf, "/dev/stdout"]) if False else None
    binf = os.path.join(out_dir, name + ".bin"); run([TC + "objcopy", "-O", "binary", "-j", ".text", elf, binf])
    img = open(binf, "rb").read()
    assert len(img) == img_end, (len(img), img_end)
    if len(img) < 16 or struct.unpack("<I", img[:4])[0] != 0x4C564F50: sys.exit(f"{name}/{plat}: image doesn't start with the OvlHeader")
    # relocations kept by -q: ABS32 words point into the overlay (symbol in a section) or the app (absolute symbol)
    rel_ovl, rel_app = [], []
    section = None
    for line in run([TC + "readelf", "-rW", elf]).splitlines():
        m = re.match(r"Relocation section '(\S+)'", line)
        if m: section = m.group(1); continue
        p = line.split()
        if len(p) < 5 or not re.fullmatch(r"[0-9a-f]{8}", p[0]): continue
        off, typ, sym = int(p[0], 16), p[2], p[4]
        if section != ".rel.text": continue
        if typ in ("R_ARM_THM_CALL", "R_ARM_THM_JUMP24", "R_ARM_CALL", "R_ARM_JUMP24"):
            # a direct branch: must stay inside the overlay
            if sym in app and sym not in mine: sys.exit(f"{name}/{plat}: direct call to app symbol {sym} (needs -mlong-calls)")
            continue
        if typ in ("R_ARM_REL32", "R_ARM_THM_JUMP11", "R_ARM_THM_JUMP8", "R_ARM_THM_JUMP19", "R_ARM_THM_PC8",
                   "R_ARM_THM_PC12", "R_ARM_THM_ALU_PREL_11_0", "R_ARM_PREL31", "R_ARM_NONE", "R_ARM_V4BX"):
            if typ == "R_ARM_REL32" and sym in app and sym not in mine: sys.exit(f"{name}/{plat}: PC-relative ref to app {sym}")
            continue
        if typ != "R_ARM_ABS32": sys.exit(f"{name}/{plat}: unsupported relocation {typ} for {sym}")
        if off >= img_end: continue
        in_app = sym in app and sym not in mine
        (rel_app if in_app else rel_ovl).append(off)
    assert len(img) < 65536
    hdr = struct.pack("<IHHHHII", 0x31564F50, len(img), end - img_end, len(rel_ovl), len(rel_app),
                      app["TERRAIN_FLAGS"], app["rnd_next"])
    data = hdr + img + struct.pack(f"<{len(rel_ovl)}H", *rel_ovl) + struct.pack(f"<{len(rel_app)}H", *rel_app)
    out = os.path.join(ROOT, "resources/data", f"ovl_{name}~{plat}.bin")
    old = open(out, "rb").read() if os.path.exists(out) else None
    if old != data: open(out, "wb").write(data)
    print(f"ovl {name:8s} {plat:7s} image {len(img)} B, bss {end - img_end} B, relocs {len(rel_ovl)}+{len(rel_app)}"
          + ("" if old != data else " (unchanged)"))
    need = len(img) + end - img_end                    # 0.30: a transient overlay must fit the 8 KB arena (src/c/ovl.c ARENA_SIZE),
    if name not in ("level",) and need > 8192:          # else it needs that much free heap, which the watch doesn't have
        sys.exit(f"ovl {name}/{plat}: {need} B is over the 8192 B overlay arena: split it")
    if name not in ("level",) and need > 7900: print(f"WARNING: ovl {name}/{plat} {need} B is close to the 8192 B arena")
    return old != data

if __name__ == "__main__":
    if sys.argv[1:] == ["--keep"]: sys.exit(3 if gen_keep() else 0)
    changed = False
    for name in sorted(os.listdir(os.path.join(ROOT, "src/ovl"))):
        for plat in PLATFORMS: changed |= build(name, plat)
    sys.exit(3 if changed else 0)
