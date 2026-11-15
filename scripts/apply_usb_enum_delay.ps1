# Apply the project-local enumeration timing experiment to the managed espressif/usb component.
$ErrorActionPreference = "Stop"

$componentPath = Join-Path $PSScriptRoot "..\managed_components\espressif__usb\src\enum.c"
if (-not (Test-Path -LiteralPath $componentPath)) {
    throw "Managed USB Host source not found: $componentPath. Run 'idf.py reconfigure' first."
}

$content = [System.IO.File]::ReadAllText($componentPath)
$marker = "USB_GATEWAY_ENUMERATION_STAGE_DELAY_MS"
if ($content.Contains($marker)) {
    Write-Host "USB enumeration timing patch is already applied."
    exit 0
}

$anchor = "    // Set nest stage of enumeration process, based on the stage pass result"
$first = $content.IndexOf($anchor, [System.StringComparison]::Ordinal)
$last = $content.LastIndexOf($anchor, [System.StringComparison]::Ordinal)
if ($first -lt 0 -or $first -ne $last) {
    throw "Expected unique enum_process() insertion point was not found. No file was changed."
}

$newline = [Environment]::NewLine
$insert = "#if CONFIG_USB_GATEWAY_ENUMERATION_STAGE_DELAY_MS > 0" + $newline +
    "    /* Project diagnostic: allow additional device response time between enum stages. */" + $newline +
    "    vTaskDelay(pdMS_TO_TICKS(CONFIG_USB_GATEWAY_ENUMERATION_STAGE_DELAY_MS));" + $newline +
    "#endif" + $newline + $newline + $anchor

$updated = $content.Remove($first, $anchor.Length).Insert($first, $insert)
$encoding = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($componentPath, $updated, $encoding)

Write-Host "Applied USB enumeration stage delay patch."
Write-Host "Configured delay: CONFIG_USB_GATEWAY_ENUMERATION_STAGE_DELAY_MS (default 10 ms)."
