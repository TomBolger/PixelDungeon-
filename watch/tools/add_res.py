"""add_res.py NAME FILE [type]: add a resource entry (raw, emery+gabbro) to package.json if missing."""
import json, sys, os
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "package.json")
d = json.load(open(p))
media = d["pebble"]["resources"]["media"]
name, f = sys.argv[1], sys.argv[2]
typ = sys.argv[3] if len(sys.argv) > 3 else "raw"
if not any(m["name"] == name for m in media):
    e = {"type": typ, "name": name, "file": f, "targetPlatforms": ["emery", "gabbro"]}
    if typ == "bitmap": e["memoryFormat"] = "Smallest"; e["spaceOptimization"] = "memory"
    media.append(e)
    json.dump(d, open(p, "w"), indent=2); open(p, "a").write("\n")
    print("added", name)
