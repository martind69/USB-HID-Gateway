Get-ChildItem -Path . -Recurse -File |
	Where-Object {
		$_.FullName -notmatch '\\(\.git|\.github|\.fulltree|build|scripts|docs|managed_components)(\\|$)' -and
		$_.Name -ne '.fulltree.txt' -and
		$_.Extension -ne '.md'
	} |
	ForEach-Object {
		"`n=== $($_.FullName) ===`n"
		Get-Content -LiteralPath $_.FullName -Raw
	} |
	Out-File -FilePath ".\.fulltree.txt" -Encoding utf8