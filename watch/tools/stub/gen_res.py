"""Write tools/stub/res_ids.h (RESOURCE_ID_* in package.json order) for host-side tools and tests."""
import json, os, sys
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
m = json.load(open(os.path.join(root, "package.json")))["pebble"]["resources"]["media"]
names = ["TEXT"] + [x["name"] for x in m if x["name"] != "TEXT"]
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "res_ids.h")
open(out, "w").write("#pragma once\nenum { " + ", ".join(f"RESOURCE_ID_{n} = {i + 1}" for i, n in enumerate(names)) + " };\n")
