$source = ".fulltree.txt"
$destination = "$env:GoogleDrive\Hobby\Software\VisualStudio\USB-HID-Gateway\fulltree.txt"

Get-ChildItem -Path . -Recurse -File |
	Where-Object {
		$_.FullName -notmatch '\\(\.git|\.github|build|scripts|docs|managed_components)(\\|$)' -and
		$_.Name -ne $source -and
		$_.Extension -ne '.md'
	} |
	ForEach-Object {
		"`n=== $($_.FullName) ===`n"
		Get-Content -LiteralPath $_.FullName -Raw
	} |
	Out-File -FilePath ".\$source" -Encoding utf8

try {
	# Zielordner sicherstellen, falls noch nicht vorhanden
	$targetDir = Split-Path -Path $destination -Parent
	if (-not (Test-Path -Path $targetDir)) {
		New-Item -Path $targetDir -ItemType Directory -Force | Out-Null
	}

	$timestamp = (Get-Date -UFormat %s) -Replace("[,\.]\d*", "")
	Copy-Item -Path $source -Destination $destination -Force -ErrorAction Stop
	Write-Output "I ($timestamp) powershell: File updated successfull"
}
catch {
	Write-Error "E ($timestamp) powershell: $($_.Exception.Message)"
}
