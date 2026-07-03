import json
import serial
import time

PORT = "COM18"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)
ser.reset_input_buffer()

rows = []
print(f"Expanded confirm on {PORT}...")
start = time.time()

while time.time() - start < 90:
    line = ser.readline().decode("utf-8", errors="replace").strip()
    if not line:
        continue
    print(line)
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        continue
    if "name" in obj:
        rows.append(obj)
    if obj.get("expanded") == "DONE":
        break

ser.close()

valid = [r for r in rows if r.get("valid")]
print(f"\nVALID sensors: {len(valid)}")
for r in valid:
    print(
        f"{r['name']:14s} TRIG {r['trig']:2d} ECHO {r['echo']:2d} "
        f"avg={r['avg']}cm ok={r['ok']}/10 spread={r['spread']}cm"
    )

if not valid:
    print("\nBest candidates:")
    rows.sort(key=lambda r: (-r.get("ok", 0), r.get("spread", 999)))
    for r in rows[:8]:
        print(
            f"{r['name']:14s} TRIG {r['trig']:2d} ECHO {r['echo']:2d} "
            f"avg={r['avg']}cm ok={r['ok']}/10 spread={r['spread']}cm"
        )