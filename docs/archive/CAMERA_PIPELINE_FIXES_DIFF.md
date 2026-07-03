# Camera & Recognition Pipeline — Fix Diff

Generated: 2026-04-11  
File: `android/BuddyBot/KidsApp/app/src/main/java/com/buddybot/kids/MainActivity.kt`

---

## Diagnosis Summary

| # | Issue | Status in current code | Fix needed |
|---|-------|------------------------|------------|
| 1 | Camera opens only after surface ready | ✅ `openCamera()` is inside `onSurfaceTextureAvailable` | None |
| 2 | `startPreview()` called via callback, not delay | ✅ Called immediately after `openCamera()` in same IO block | None |
| 3 | Detection runs on `Dispatchers.Default` | ✅ `lifecycleScope.launch(Dispatchers.Default)` | None |
| 4 | `FACE:x,y` throttled to max once per 200 ms | ❌ No throttle — fires on every frame | Add `lastFaceSentMs` guard |
| 5 | `OBJ:label,confidence` throttled to max once per 200 ms | ❌ No throttle — fires on every object per frame | Add `lastObjSentMs` guard |
| 6 | Camera released in `onPause()`, reopened in `onResume()` | ❌ `onPause()` only calls `stopPreview()`; `onResume()` never reopens | `closeCamera()` in `onPause()`, `initializeUSBWebcam()` in `onResume()` |
| 7 | Null check on every camera frame | ✅ Callback already has `data?.let { processUVCFrame(it) }` | None |

---

## Fix 1 — Add throttle timestamps (new fields, ~line 135)

```diff
-    private val isMlProcessing = AtomicBoolean(false)
+    private val isMlProcessing = AtomicBoolean(false)
+    // Throttle FACE: and OBJ: commands to Mega — max once per 200 ms each
+    @Volatile private var lastFaceSentMs = 0L
+    @Volatile private var lastObjSentMs  = 0L
+    private val VISION_THROTTLE_MS = 200L
```

---

## Fix 2 — Throttle FACE: send (~line 816–818)

```diff
-                        val nx = (result.bounds.centerX() * 1000 / 640).coerceIn(0, 1000)
-                        val ny = (result.bounds.centerY() * 1000 / 480).coerceIn(0, 1000)
-                        arduinoComms.sendCommand("FACE:$nx,$ny")
+                        val now = System.currentTimeMillis()
+                        if (now - lastFaceSentMs >= VISION_THROTTLE_MS) {
+                            lastFaceSentMs = now
+                            val nx = (result.bounds.centerX() * 1000 / 640).coerceIn(0, 1000)
+                            val ny = (result.bounds.centerY() * 1000 / 480).coerceIn(0, 1000)
+                            arduinoComms.sendCommand("FACE:$nx,$ny")
+                        }
```

---

## Fix 3 — Throttle OBJ: send (~line 835–838)

```diff
-                    val rawLabel = obj.labels.firstOrNull()?.text ?: "Unknown"
-                    val safeLabel = rawLabel.replace(",", "").replace(" ", "_")
-                    val confidence = "%.2f".format(obj.labels.firstOrNull()?.confidence ?: 0f)
-                    arduinoComms.sendCommand("OBJ:$safeLabel,$confidence")
+                    val nowObj = System.currentTimeMillis()
+                    if (nowObj - lastObjSentMs >= VISION_THROTTLE_MS) {
+                        lastObjSentMs = nowObj
+                        val rawLabel = obj.labels.firstOrNull()?.text ?: "Unknown"
+                        val safeLabel = rawLabel.replace(",", "").replace(" ", "_")
+                        val confidence = "%.2f".format(obj.labels.firstOrNull()?.confidence ?: 0f)
+                        arduinoComms.sendCommand("OBJ:$safeLabel,$confidence")
+                    }
```

---

## Fix 4 — Camera lifecycle: onPause() closes camera (~line 905–911)

```diff
     override fun onPause() {
         super.onPause()
         sensorManager.unregisterListener(this)
-        // FIX #8: stop the camera preview when the app is backgrounded so the UVC
-        // driver releases the frame buffer and does not keep streaming while invisible.
-        usbCameraClient?.stopPreview()
+        // Release the camera fully in onPause so the UVC driver is not held while
+        // the app is backgrounded. It will be reopened in onResume().
+        try {
+            usbCameraClient?.closeCamera()
+        } catch (e: Exception) {
+            Log.w(TAG, "Camera close in onPause failed (ignored): ${e.message}")
+        }
     }
```

---

## Fix 5 — Camera lifecycle: onResume() reopens camera (~line 894–903)

```diff
     override fun onResume() {
         super.onResume()
         orientationSensor?.also {
             sensorManager.registerListener(
                 this,
                 it,
                 SensorManager.SENSOR_DELAY_UI
             )
         }
+        // Reopen the USB webcam if it was closed in onPause().
+        // Guard with a null check: if usbCameraClient is null the app hasn't finished
+        // initialising yet and initializeUSBWebcam() will be called by startMainProgram().
+        if (usbCameraClient != null) {
+            initializeUSBWebcam()
+        }
     }
```

---

## No changes required in UIComponents.kt

The settings preview `AndroidView` already calls `webcamClient.openCamera(textureView)` inside
`lifecycleScope.launch(Dispatchers.IO)` which is correct — the TextureView surface is ready by
the time the `factory` lambda runs inside `AndroidView`.
