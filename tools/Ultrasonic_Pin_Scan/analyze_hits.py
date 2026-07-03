import json
import re
import sys
from collections import defaultdict

path = sys.argv[1] if len(sys.argv) > 1 else r"D:\BuddyBot\terminals\3.txt"
text = open(path, encoding="utf-8", errors="replace").read()

hits = []
for line in text.splitlines():
    line = line.strip()
    if not line.startswith('{"hit"'):
        continue
    try:
        hits.append(json.loads(line))
    except json.JSONDecodeError:
        pass

# Group by echo pin — real sensors usually have a stable echo line.
by_echo = defaultdict(list)
for h in hits:
    by_echo[h["echo"]].append(h)

print(f"Total raw hits: {len(hits)}")
print("\nHigh-confidence (samples_ok >= 2):")
strong = [h for h in hits if h.get("samples_ok", 0) >= 2]
strong.sort(key=lambda h: (-h["samples_ok"], h["trig"], h["echo"]))
for h in strong:
    print(
        f"  TRIG {h['trig']:2d}  ECHO {h['echo']:2d}  "
        f"dist={h['dist_cm']}cm  samples={h['samples_ok']}"
    )

print("\nEcho pins with multiple TRIG hits (likely real ECHO lines):")
for echo, group in sorted(by_echo.items(), key=lambda kv: -len(kv[1])):
    if len(group) >= 3:
        dists = sorted({g["dist_cm"] for g in group})
        print(f"  ECHO {echo:2d}: {len(group)} hits, distances {dists[:6]}")
        best = max(group, key=lambda g: g.get("samples_ok", 0))
        print(
            f"         best TRIG {best['trig']:2d} "
            f"({best['dist_cm']}cm, samples={best['samples_ok']})"
        )