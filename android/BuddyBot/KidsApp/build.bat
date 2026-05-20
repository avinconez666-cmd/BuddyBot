@echo off
REM Build script for BuddyBot Kids - Automatically uses Java 17 if available

REM Priority: User provided JDK path
if exist "D:\MOVED_PROGRAMS\Eclipse Adoptium\jdk-17.0.18+8" (
    set JAVA_HOME=D:\MOVED_PROGRAMS\Eclipse Adoptium\jdk-17.0.18+8
    echo Using Java 17 from: %JAVA_HOME%
) else if exist "C:\Program Files\Eclipse Adoptium\jdk-17.0.13.11-hotspot" (
    set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-17.0.13.11-hotspot
    echo Using Java 17 from: %JAVA_HOME%
) else if exist "C:\Program Files\Eclipse Adoptium\jdk-11.0.28.6-hotspot" (
    echo Java 17 not found. Attempting to use Java 11...
    set JAVA_HOME=C:\Program Files\Eclipse Adoptium\jdk-11.0.28.6-hotspot
) else (
    echo ERROR: Neither Java 17 nor Java 11 found!
    echo Please install Java 17 from https://adoptium.net/
    exit /b 1
)

set PATH=%JAVA_HOME%\bin;%PATH%

REM Add Git's Unix tools to PATH so NDK's ndk-build can find 'uname'
REM (libuvc uses the legacy ndk-build system which calls 'uname -s' to detect the OS)
if exist "C:\Program Files\Git\usr\bin\uname.exe" (
    set PATH=C:\Program Files\Git\usr\bin;%PATH%
    echo Added Git Unix tools to PATH for NDK build ^(uname found^)
) else (
    echo WARNING: uname.exe not found - libuvc ndkBuild may fail.
    echo Install Git for Windows: https://git-scm.com/download/win
)

REM Clear problematic environment variable
set JAVA_TOOL_OPTIONS=

REM Run Gradle
echo.
echo Building BuddyBot Kids...
echo.

call gradlew.bat clean assembleDebug %*

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo BUILD SUCCESSFUL!
    echo APK location: app\build\outputs\apk\debug\app-debug.apk
    echo ========================================
) else (
    echo.
    echo BUILD FAILED!
    echo Try installing Java 17: https://adoptium.net/
    exit /b 1
)
