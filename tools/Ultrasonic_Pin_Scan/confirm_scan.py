import json
import serial
import time

PORT = "COM18"
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)
ser.reset_input_buffer()

print(f"Confirm scan on {PORT}...")
rows = []
done = False
start = time.time()

while time.time() - start < 60:
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
    if obj.get("confirm") == "DONE":
        done = True
        break

ser.close()

print("\n--- LIKELY SENSORS (valid=true) ---")
valid = [r for r in rows if r.get("valid") == "true"]
if not valid:
    print("(none passed strict filter; showing best candidates)")
    rows.sort(key=lambda r: (-r.get("ok", 0), r.get("spread", 999)))
    for r in rows[:8]:
        print(
            f"{r['name']:12s} TRIG {r['trig']:2d} ECHO {r['echo']:2d} "
            f"ok={r['ok']}/8 avg={r['avg']}cm spread={r['spread']}cm"
        )
else:
    for r in valid:
        print(
            f"{r['name']:12s} TRIG {r['trig']:2d} ECHO {r['echo']:2d} "
            f"avg={r['avg']}cm spread={r['spread']}cm"
        )