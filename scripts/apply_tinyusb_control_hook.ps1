# Patch the pinned TinyUSB Device control dispatcher with a generic SETUP hook.
$ErrorActionPreference = "Stop"

$componentRoot = Join-Path $PSScriptRoot "..\managed_components\espressif__esp_tinyusb"
if (-not (Test-Path -LiteralPath $componentRoot)) {
	throw "Managed esp_tinyusb component not found. Run idf.py reconfigure first."
}

$versionHeader = Join-Path $componentRoot "tinyusb\src\tusb_option.h"
if (-not (Test-Path -LiteralPath $versionHeader)) {
	throw "TinyUSB version header not found: $versionHeader"
}

$versionContent = [System.IO.File]::ReadAllText($versionHeader)
$requiredVersion = @(
	'#define TUSB_VERSION_MAJOR     0',
	'#define TUSB_VERSION_MINOR     21',
	'#define TUSB_VERSION_REVISION  0'
)
foreach ($line in $requiredVersion) {
	if (-not $versionContent.Contains($line)) {
		throw "Unexpected TinyUSB version. This patch is validated for TinyUSB 0.21.0~6 (esp_tinyusb 3.0.1)."
	}
}

$sourcePath = Join-Path $componentRoot "tinyusb\src\device\usbd.c"
if (-not (Test-Path -LiteralPath $sourcePath)) {
	throw "TinyUSB Device dispatcher not found: $sourcePath"
}

$content = [System.IO.File]::ReadAllText($sourcePath)
$marker = "USB_GATEWAY_GENERIC_CONTROL_REQUEST_HOOK"
if ($content.Contains($marker)) {
	Write-Host "Generic TinyUSB control request hook is already applied."
	exit 0
}

$vendorStub = @'
TU_ATTR_WEAK bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
  (void) rhport; (void) stage; (void) request;
  return false;
}
'@

$genericStub = @'
/* USB_GATEWAY_GENERIC_CONTROL_REQUEST_HOOK */
TU_ATTR_WEAK bool tud_control_request_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
  (void) rhport; (void) stage; (void) request;
  return false;
}
'@

if (-not $content.Contains($vendorStub)) {
	throw "Expected TinyUSB vendor callback definition was not found. No file was changed."
}
$content = $content.Replace($vendorStub, $vendorStub + "`r`n" + $genericStub)

$setupAnchor = @'
  TU_ASSERT(p_request->bmRequestType_bit.type < TUSB_REQ_TYPE_INVALID);

  // Vendor request
'@
$setupReplacement = @'
  TU_ASSERT(p_request->bmRequestType_bit.type < TUSB_REQ_TYPE_INVALID);

  // Allow the proxy to take ownership before TinyUSB standard/class dispatch.
  ctrl_xfer->complete_cb = tud_control_request_cb;
  if (tud_control_request_cb(rhport, CONTROL_STAGE_SETUP, p_request)) {
    return true;
  }
  ctrl_xfer->complete_cb = NULL;

  // Vendor request
'@

if (-not $content.Contains($setupAnchor)) {
	throw "Expected process_setup_received() dispatch point was not found. No file was changed."
}
$content = $content.Replace($setupAnchor, $setupReplacement)

$encoding = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($sourcePath, $content, $encoding)

Write-Host "Applied generic TinyUSB control request hook."
Write-Host "Validated TinyUSB version: 0.21.0 (esp_tinyusb 3.0.1)."
