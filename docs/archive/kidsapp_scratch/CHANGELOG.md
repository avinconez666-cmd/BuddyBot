# Changelog

All notable changes to BuddyBot Kids project.

## [2.0.0] - 2026-01-29

### ✨ NEW FEATURES ADDED

#### Core Features
- ✅ **Call Daddy Button** - Green video call button with your photo
  - Opens Messenger for video calls
  - Falls back to phone call if Messenger unavailable
  - Sends notification when initiated

- ✅ **Face Recognition** - USB webcam integration
  - ML Kit face detection
  - TensorFlow Lite for recognition
  - Distinguishes family members from strangers
  - Real-time processing at 1 FPS

- ✅ **Object Detection** - Environment awareness
  - Detects toys, people, pets
  - ML Kit object detection
  - Stream mode for real-time updates

- ✅ **Environment Monitoring Service** - 24/7 audio surveillance
  - **Arguments** - Detects conflict phrases + volume spikes
  - **Swearing** - Keyword matching for inappropriate language
  - **Yelling** - Volume threshold detection (>70%)
  - **Child Distress** - Emergency phrases like "help", "stop"
  - **Extended Silence** - Alerts after 5+ minutes of quiet
  - **Stranger Detection** - Unknown face notifications
  
#### Additional Features
- Emergency SOS button with auto-call
- Multiple robot personality modes
- Auto-start on device boot
- Comprehensive notification system
- Real-time parent alerts
- USB Serial Arduino communication
- CameraX integration for external webcam

### 🔧 FIXES & IMPROVEMENTS

#### Security
- ✅ **API Keys Secured** - Removed hardcoded keys
  - BuildConfig injection via Gradle
  - secrets.properties for configuration
  - .gitignore prevents accidental commits
  - Template file for easy setup

#### Build Configuration
- ✅ Fixed Compose compiler version (1.5.8 → 1.5.10)
- ✅ Updated to match Kotlin 1.9.22
- ✅ Added JitPack repository for USB Serial
- ✅ Proper buildFeatures configuration
- ✅ BuildConfig enabled

#### Project Structure
- ✅ Deleted empty kotlin/ folder
- ✅ Fixed video file name constants
  - idle.mp4 → normal_idle.mp4
  - talking.mp4 → normal_talk.mp4
  - surprised.mp4 → normal_surprised.mp4
- ✅ Proper AndroidManifest permissions
- ✅ Added all required XML resources

#### Code Quality
- ✅ Comprehensive error handling
- ✅ Proper memory management (MediaPlayer cleanup)
- ✅ Lifecycle-aware components
- ✅ Coroutine scope management
- ✅ Try-catch blocks around API calls
- ✅ Null safety checks throughout

#### Dependencies Added
- androidx.camera (CameraX) for USB webcam
- ML Kit face detection
- ML Kit object detection
- TensorFlow Lite for face recognition
- Firebase Cloud Messaging
- WorkManager for background tasks
- Coil for image loading
- DataStore for preferences
- JTransforms for audio processing

### 📱 UI/UX Improvements
- Jetpack Compose overlay UI
- Status indicators (listening, processing, speaking)
- Mode selector buttons
- Call Daddy button with icon
- Emergency SOS button
- Real-time alerts display
- Clean, modern Material 3 design

### 📚 Documentation
- ✅ Complete SETUP_GUIDE.md (2-hour setup process)
- ✅ Comprehensive README.md
- ✅ API key configuration template
- ✅ Troubleshooting guide
- ✅ Code comments throughout
- ✅ Architecture documentation

### 🔐 Permissions
Added proper permission declarations:
- CAMERA - Face recognition
- RECORD_AUDIO - Voice & monitoring
- CALL_PHONE - Emergency calls
- POST_NOTIFICATIONS - Alerts
- FOREGROUND_SERVICE - Background monitoring
- USB_PERMISSION - Arduino & webcam
- RECEIVE_BOOT_COMPLETED - Auto-start

### 🏗️ Architecture Changes
- Service-based environment monitoring
- Foreground service for 24/7 operation
- Boot receiver for auto-start
- Firebase messaging integration
- Modular ML managers
- Separated concerns (services, managers, receivers)

### 📦 New Files Created
- EnvironmentMonitoringService.kt - Audio monitoring
- FaceRecognitionManager.kt - Face ML
- ObjectDetectionManager.kt - Object ML
- BuddyBotMessagingService.kt - Push notifications
- BootReceiver.kt - Auto-start
- USB device filter XML
- Backup rules
- Data extraction rules
- Multiple drawable resources
- Complete string resources

## [1.0.0] - Original Version

### Features (Original)
- ElevenLabs voice synthesis
- Claude AI conversations
- Basic video playback
- USB Serial communication
- Multiple video states
- Basic UI

### Issues (Original)
- ⚠️ Hardcoded API keys (SECURITY RISK)
- ⚠️ Empty kotlin/ folder
- ⚠️ Missing Gradle wrapper
- ⚠️ Incorrect video file names
- ⚠️ No error handling
- ⚠️ Memory leaks in MediaPlayer
- ⚠️ No face recognition
- ⚠️ No environment monitoring
- ⚠️ No parent notifications
- ⚠️ Limited documentation

---

## Migration Guide: v1.0 → v2.0

### Breaking Changes
1. **API Keys:** Must be moved to secrets.properties
2. **Video Files:** Rename to match new constants
3. **Permissions:** Additional permissions required

### Migration Steps
1. Create secrets.properties from template
2. Move API keys from code to secrets file
3. Rename video files or update constants
4. Add google-services.json for Firebase
5. Rebuild project
6. Grant all new permissions

### Deprecated
- Hardcoded API keys (removed)
- Empty kotlin/ source folder (removed)

### New Requirements
- Android Studio Arctic Fox or newer
- Gradle 8.2.1
- Kotlin 1.9.22
- USB webcam (UVC compatible)
- Firebase project for notifications

---

## Upcoming Features (v2.1+)

### Planned
- Cloud face recognition database
- Advanced speech-to-text integration
- Parent web dashboard
- More educational games
- Multi-language support
- Custom wake words

### Under Consideration
- Emotion detection
- Gesture recognition
- Multiple robot coordination
- AR learning experiences
- Sleep schedule integration

---

**Note:** This project is in active development. Features and APIs may change.
