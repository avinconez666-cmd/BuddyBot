# Builds BuddyBot Kids debug APK and copies it somewhere easy to grab for Phone Link.
#
# Usage:
#   .\deploy-apk.ps1              # incremental build + copy
#   .\deploy-apk.ps1 -Clean       # clean build + copy
#   .\deploy-apk.ps1 -SkipBuild   # copy last build only
#
# Output copies:
#   dist\BuddyBot-Kids-debug.apk           (project folder)
#   %USERPROFILE%\Downloads\BuddyBot-Kids-debug.apk   (easy Phone Link drag target)

param(
    [switch]$Clean,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$ApkSource = Join-Path $ProjectRoot "app\build\outputs\apk\debug\app-debug.apk"
$DistDir = Join-Path $ProjectRoot "dist"
$DistApk = Join-Path $DistDir "BuddyBot-Kids-debug.apk"
$DownloadsApk = Join-Path $env:USERPROFILE "Downloads\BuddyBot-Kids-debug.apk"

function Set-JavaHome {
    $candidates = @(
        "D:\MOVED_PROGRAMS\Eclipse Adoptium\jdk-17.0.18+8",
        "C:\Program Files\Eclipse Adoptium\jdk-17.0.13.11-hotspot",
        "C:\Program Files\Eclipse Adoptium\jdk-11.0.28.6-hotspot"
    )
    foreach ($path in $candidates) {
        if (Test-Path $path) {
            $env:JAVA_HOME = $path
            $env:PATH = "$path\bin;$env:PATH"
            Write-Host "Using Java from: $path"
            return
        }
    }
    throw "No supported JDK found. Install Java 17 from https://adoptium.net/"
}

function Add-NdkPath {
    $uname = "C:\Program Files\Git\usr\bin\uname.exe"
    if (Test-Path $uname) {
        $gitBin = "C:\Program Files\Git\usr\bin"
        if ($env:PATH -notlike "*$gitBin*") {
            $env:PATH = "$gitBin;$env:PATH"
        }
        Write-Host "NDK helper tools available (Git uname)"
    } else {
        Write-Warning "Git uname not found - libuvc NDK build may fail. Install Git for Windows."
    }
    $env:JAVA_TOOL_OPTIONS = ""
}

function Build-Apk {
    Push-Location $ProjectRoot
    try {
        if ($Clean) {
            Write-Host "`nClean building debug APK..."
            & .\gradlew.bat clean assembleDebug
        } else {
            Write-Host "`nBuilding debug APK..."
            & .\gradlew.bat assembleDebug
        }
        if ($LASTEXITCODE -ne 0) {
            throw "Gradle build failed (exit $LASTEXITCODE)"
        }
    } finally {
        Pop-Location
    }
}

function Copy-ApkArtifacts {
    if (-not (Test-Path $ApkSource)) {
        throw "APK not found at $ApkSource - build may have failed."
    }

    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

    Copy-Item -Path $ApkSource -Destination $DistApk -Force
    Copy-Item -Path $ApkSource -Destination $DownloadsApk -Force

    $info = Get-Item $DistApk
    $sizeMb = [math]::Round($info.Length / 1MB, 1)

    Write-Host ""
    Write-Host "========================================"
    Write-Host " APK ready for Phone Link ($sizeMb MB)"
    Write-Host "========================================"
    Write-Host " Project:   $DistApk"
    Write-Host " Downloads: $DownloadsApk"
    Write-Host ""
    Write-Host " Phone Link install:"
    Write-Host "   1. Open Phone Link -> Files (or drag from Downloads in Explorer)"
    Write-Host "   2. Copy BuddyBot-Kids-debug.apk to the S9 (e.g. Download folder)"
    Write-Host "   3. On the S9, tap the APK -> Install"
    Write-Host "   4. Allow 'Install unknown apps' if prompted"
    Write-Host ""
    Write-Host " Tip: re-run .\deploy-apk.ps1 after code changes to refresh the APK."
    Write-Host ""
}

try {
    Set-JavaHome
    Add-NdkPath

    if (-not $SkipBuild) {
        Build-Apk
    } elseif (-not (Test-Path $ApkSource)) {
        throw "No existing APK at $ApkSource. Run without -SkipBuild first."
    } else {
        Write-Host "Skipping build - copying existing APK..."
    }

    Copy-ApkArtifacts
} catch {
    Write-Error $_
    exit 1
}