import json
import serial
import time

PORT = "COM18"
BAUD = 115200
TIMEOUT_S = 300

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(2)
ser.reset_input_buffer()
print(f"Listening on {PORT}...")

start = time.time()
hits = []
done = False

while time.time() - start < TIMEOUT_S:
    line = ser.readline().decode("utf-8", errors="replace").strip()
    if not line:
        continue
    print(line)
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        continue

    if obj.get("hit"):
        hits.append(obj)
    if obj.get("scan") == "DONE":
        done = True
        break

ser.close()

print("--- SUMMARY ---")
print(f"done={done} hits={len(hits)}")
for h in hits:
    print(
        f"TRIG {h['trig']:2d}  ECHO {h['echo']:2d}  "
        f"dist={h['dist_cm']}cm  samples={h['samples_ok']}"
    )