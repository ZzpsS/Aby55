param(
    [string]$Port = "COM3",
    [int]$Baud = 460800,
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (Test-Path (Join-Path $Root "firmware")) {
    $PackageRoot = $Root
} else {
    $PackageRoot = Split-Path -Parent $Root
}

$FirmwareDir = Join-Path $PackageRoot "firmware"
$Bootloader = Join-Path $FirmwareDir "bootloader.bin"
$PartitionTable = Join-Path $FirmwareDir "partition-table.bin"
$App = Join-Path $FirmwareDir "tab5_drummer.bin"

foreach ($File in @($Bootloader, $PartitionTable, $App)) {
    if (!(Test-Path $File)) {
        throw "Missing firmware file: $File"
    }
}

Write-Host "Flashing Aby55 to $Port at $Baud baud..."
& $Python -m esptool --chip esp32p4 -p $Port -b $Baud --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_freq 80m --flash_size 16MB `
    0x2000 $Bootloader `
    0x8000 $PartitionTable `
    0x10000 $App

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Flashing failed. Make sure Python and esptool are installed, or pass -Python with the ESP-IDF Python path."
    exit $LASTEXITCODE
}

Write-Host "Flash complete. The Tab5 should reboot into Aby55."
