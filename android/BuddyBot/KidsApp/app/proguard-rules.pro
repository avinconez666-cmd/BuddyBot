# BuddyBot ProGuard Rules

# 1. BuildConfig
-keep class com.buddybot.kids.BuildConfig { *; }

# 2. ML Kit & TensorFlow (Crucial for Vision)
-keep class com.google.mlkit.** { *; }
-keep class org.tensorflow.** { *; }
-keep class com.google.android.gms.internal.mlkit_vision_face.** { *; }

# 3. USB Serial
-keep class com.felhr.** { *; }

# 4. Networking (OkHttp)
-dontwarn okhttp3.**
-keep class okhttp3.** { *; }

# 5. Firebase
-keep class com.google.firebase.** { *; }

# 6. Media3 / ExoPlayer (FIX: Added per reference)
-keep class androidx.media3.** { *; }
-dontwarn androidx.media3.**
-keep interface androidx.media3.common.Player$Listener { *; }

# 7. JSON Models (FIX: Prevent obfuscation of telemetry/state fields)
-keepclassmembers class com.buddybot.kids.RobotState { *; }
-keepclassmembers class com.buddybot.kids.TelemetryData { *; }
-keep class com.buddybot.kids.RobotMode { *; }
-keep class com.buddybot.kids.CommunicationMode { *; }
-keep class com.buddybot.kids.AIService { *; }
