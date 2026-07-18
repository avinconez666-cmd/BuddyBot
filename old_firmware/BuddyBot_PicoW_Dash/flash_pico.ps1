$uf2 = Join-Path $PSScriptRoot "build\BuddyBot_PicoW_Dash.ino.uf2"
if (-not (Test-Path $uf2)) { Write-Error "Missing UF2"; exit 1 }
Write-Host "UF2: $uf2"
Write-Host "Hold BOOTSEL, plug Pico USB, waiting 120s..."
$deadline = (Get-Date).AddSeconds(120)
while ((Get-Date) -lt $deadline) {
    foreach ($letter in @('D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z')) {
        $info = "${letter}:\INFO_UF2.TXT"
        if (Test-Path $info) {
            Copy-Item $uf2 "${letter}:\BuddyBot_PicoW_Dash.ino.uf2" -Force
            Write-Host "Flashed ${letter}:\BuddyBot_PicoW_Dash.ino.uf2"
            exit 0
        }
    }
    Start-Sleep -Milliseconds 500
}
Write-Host "No BOOTSEL drive found."
exit 1