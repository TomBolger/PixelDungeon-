"""Where the upstream Pixel Dungeon sources live (the third_party/pixel-dungeon submodule).
Override with the PD_ROOT environment variable to point at another checkout."""
import os
REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PD_ROOT = os.environ.get("PD_ROOT") or os.path.join(REPO, "third_party", "pixel-dungeon")
PD_SRC = os.path.join(PD_ROOT, "src", "com", "watabou", "pixeldungeon")   # the Java game code
PD_ASSETS = os.path.join(PD_ROOT, "assets")                               # sprites, music, sounds
