param(
    [Parameter(Mandatory = $true)]
    [string]$Manifest
)

# Remove the two "headless" Application entries (AppListEntry="none"):
#   - NanaZip.Console  (CLI: NanaZipC.exe / K7C.exe / 7z.exe)
#   - NanaZip.Windows  (shell-launched G variant: NanaZipG.exe / K7G.exe / 7zG.exe)
# The main GUI Application is left untouched, so the package stays valid.

$b = [System.IO.File]::ReadAllBytes($Manifest)
$hasBom = ($b.Length -ge 3 -and $b[0] -eq 0xEF -and $b[1] -eq 0xBB -and $b[2] -eq 0xBF)
$enc = New-Object System.Text.UTF8Encoding($hasBom)
$t = [System.IO.File]::ReadAllText($Manifest, $enc)

foreach ($id in @('NanaZip.Console', 'NanaZip.Windows')) {
    $q = [char]34
    $re = '(?s)<Application\r?\n\s*Id=' + $q + [regex]::Escape($id) + $q + '.*?</Application>\r?\n'
    $t = [regex]::Replace($t, $re, '')
}

[System.IO.File]::WriteAllText($Manifest, $t, $enc)
Write-Host "stripped: $Manifest"
