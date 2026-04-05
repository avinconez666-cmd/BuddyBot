# PowerShell script to automate Java 17 upgrade and build
# Run this script as Administrator: Right-click the .ps1 file and select "Run as administrator"

# Define variables
$url = "https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.18%2B8/OpenJDK17U-jdk_x64_windows_hotspot_17.0.18_8.zip"
$installerPath = "$env:TEMP\jdk17_$(Get-Date -Format 'yyyyMMddHHmmss').zip"
$javaHome = "C:\AndroidJDK17\jdk-17.0.18+8-hotspot"
$projectPath = "D:\PATHWITHNOSPACESFORBUDDYBOT\BuddyBot\KidsApp"

# Download the JDK zip
Write-Host "Downloading JDK 17 zip..."
try {
    Invoke-WebRequest -Uri $url -OutFile $installerPath
} catch {
    Write-Host "Download failed: $_"
    exit 1
}

# Check if download succeeded
if (!(Test-Path $installerPath)) {
    Write-Host "Installer file not found after download."
    exit 1
}

# Extract the JDK
Write-Host "Extracting JDK 17..."
try {
    Expand-Archive -Path $installerPath -DestinationPath "C:\AndroidJDK17" -Force
} catch {
    Write-Host "Extraction failed: $_"
    exit 1
}

# Check if extraction succeeded
if (!(Test-Path "$javaHome\bin\java.exe")) {
    Write-Host "Java extraction verification failed. Java.exe not found at $javaHome\bin\java.exe"
    exit 1
}

# Set JAVA_HOME environment variable
Write-Host "Setting JAVA_HOME..."
[Environment]::SetEnvironmentVariable("JAVA_HOME", $javaHome, "Machine")

# Add JDK to PATH
Write-Host "Adding JDK to PATH..."
$path = [Environment]::GetEnvironmentVariable("Path", "Machine")
if ($path -notlike "*$javaHome\bin*") {
    $newPath = "$path;$javaHome\bin"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "Machine")
}

# Refresh environment variables in current session
$env:JAVA_HOME = $javaHome
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine")

# Verify installation
Write-Host "Verifying Java installation..."
java -version

# Build the project
Write-Host "Building the project..."
Set-Location $projectPath
.\build.bat

Write-Host "Upgrade and build complete!"
