import json
import serial
import time

PORT = "COM18"
BAUD = 115200
TIMEOUT_S = 600

TARGETS = [
    ("RIGHT", 14),
    ("REAR", 22),
    ("LEFT", 27),
    ("FRONT", 49),
]

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)
ser.reset_input_buffer()

hits = []
print(f"Scanning {PORT}...")
start = time.time()

while time.time() - start < TIMEOUT_S:
    line = ser.readline().decode("utf-8", errors="replace").strip()
    if not line:
        continue
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        continue
    if "trig" in obj and "echo" in obj:
        hits.append(obj)
        print(line)
    if obj.get("scan") == "DONE":
        break

ser.close()

hits.sort(key=lambda h: h["cm"])
print(f"\nAll stable echo lines found: {len(hits)}")
for h in hits:
    print(f"  TRIG {h['trig']:2d}  ECHO {h['echo']:2d}  -> {h['cm']} cm  (spread {h.get('spread', '?')})")

# Pick 4 pairs whose distances best match the four targets (greedy, no reuse)
unused = hits[:]
mapping = {}
for name, target in TARGETS:
    if not unused:
        mapping[name] = None
        continue
    pick = min(unused, key=lambda h: abs(h["cm"] - target))
    mapping[name] = pick
    unused.remove(pick)

print("\n=== YOUR 4 ULTRASONIC SENSORS ===")
for name, target in TARGETS:
    h = mapping.get(name)
    if not h:
        print(f"{name:6s}: could not identify")
        continue
    err = abs(h["cm"] - target)
    print(
        f"{name:6s}: TRIG = {h['trig']}, ECHO = {h['echo']} "
        f"(measured {h['cm']} cm, expected ~{target} cm)"
    )