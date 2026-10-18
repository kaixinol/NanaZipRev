<#
##
## PROJECT:    NanaZip Rev
## FILE:       Install.ps1
## PURPOSE:    Install, update or remove a NanaZip Rev MSIX package
##
## LICENSE:    The MIT License
##
## MAINTAINER: kaesinol <keiplyer@gmail.com>
##

.SYNOPSIS
  Installs, updates or removes a NanaZip Rev MSIX package.

.DESCRIPTION
  Downloads the newest package published on GitHub, or takes a local one, makes
  sure this machine can deploy it, and deploys it. With -Uninstall it removes
  the package, the provisioned copy that new accounts would pick up, the signing
  certificate and any certificate backup an earlier version left behind, so
  nothing is left behind.

  A published package carries no signature, so one is made on the machine,
  the package is signed with it and it is trusted. Its private key stays in
  Cert:\CurrentUser\My and is never exported. A package that already
  carries a signature keeps it, and its certificate is trusted here instead.

  Where the package comes from, first hit wins:
    1. -LocalPath
    2. $env:NANAZIPREV_MSIX, which holds a local package path
    3. the one msixbundle the latest release on -Repo carries, downloaded
    4. the build output next to this script, with a note saying so

.PARAMETER Uninstall
  Removes everything this script added.

.PARAMETER LocalPath
  Uses this package instead of looking for one.

.PARAMETER NoSign
  Leaves the package alone even when it is not signed.

.PARAMETER ForceUpdateFromAnyVersion
  Updates over the installed package instead of removing it first.

.PARAMETER PackageName
  The package family to deploy or remove. Defaults to the fork.

.PARAMETER Publisher
  The certificate subject to sign with, and the subject to remove on uninstall.

.PARAMETER Repo
  The GitHub repository releases are read from.

.PARAMETER LocalSearchRoot
  Where to look for a locally built package. Empty means the build output next
  to this script.

.PARAMETER Years
  How long the generated certificate stays valid.

.PARAMETER RemovalTimeoutSeconds
  How long to wait for the package to be removed before giving up.

.EXAMPLE
  Install or update from the latest release.
  .\Install.ps1

.EXAMPLE
  Install a package that was built locally.
  $built = Get-ChildItem .\Output\Binaries\AppPackages -Recurse
            -Filter *.msixbundle
  .\Install.ps1 -LocalPath $built[0].FullName

.EXAMPLE
  Remove the package, its certificate and any old backup.
  .\Install.ps1 -Uninstall

.NOTES
  The signing certificate is a local test one. It is created on demand, reused
  while it is in Cert:\CurrentUser\My, and rebuilt and re-signed whenever it is
  gone, so its private key is never exported to disk.

  Deploying a package this script signed itself needs sideloading, so either
  developer mode or the Allow sideloading of apps policy. Runs on Windows
  PowerShell 5.1 and PowerShell 7.1+; PowerShell 7 uses the Windows PowerShell
  compatibility layer for Appx cmdlets. Provisioned package cleanup uses 5.1.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$LocalPath,
  [switch]$Uninstall,
  [switch]$NoSign,
  [switch]$ForceUpdateFromAnyVersion,
  [string]$PackageName = 'Kaesinol.NanaZipRev',
  [ValidateSet('CN=23EBD860-A383-4AEA-836A-B041352C93FF')]
  [string]$Publisher = 'CN=23EBD860-A383-4AEA-836A-B041352C93FF',
  [string]$Repo = 'kaixinol/NanaZipRev',
  [string]$LocalSearchRoot,
  [int]$Years = 5,
  [int]$RemovalTimeoutSeconds = 180
)

# Keep -WhatIf working when the script is invoked through a pipeline.
$WhatIf = [bool]$WhatIfPreference
function Invoke-ShouldProcess {
  param($Target, $Action)
  if (-not $WhatIf) { return $true }
  Write-Host "WhatIf: would $Action $Target" -ForegroundColor DarkGray
  return $false
}

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if ($PSVersionTable.PSEdition -eq 'Core') {
  if ($PSVersionTable.PSVersion -lt [version]'7.1') {
    throw 'PowerShell 7.1 or newer is required for the Appx compatibility layer; run this script in Windows PowerShell 5.1 instead.'
  }
  Import-Module Appx -UseWindowsPowerShell -ErrorAction Stop
}

[Net.ServicePointManager]::SecurityProtocol =
  [Net.ServicePointManager]::SecurityProtocol -bor
  [Net.SecurityProtocolType]::Tls12

# Honor proxy settings when invoked through `irm ... | iex`.
$Proxy = if ($env:HTTPS_PROXY) { $env:HTTPS_PROXY }
         elseif ($env:HTTP_PROXY) { $env:HTTP_PROXY }
         elseif ($env:ALL_PROXY) { $env:ALL_PROXY }
         else { $null }

# A piped invocation has no $PSScriptRoot, so local-build fallback is unavailable.
if (-not $LocalSearchRoot -and $PSScriptRoot) {
  $LocalSearchRoot = Join-Path $PSScriptRoot 'Output\Binaries\AppPackages'
}
$localAppData = [Environment]::GetFolderPath('LocalApplicationData')
if (-not $localAppData) {
  throw 'LOCALAPPDATA could not be resolved; refusing relative cache paths'
}
$Script:NanaZipDataRoot = Join-Path $localAppData 'NanaZipRev'
$Script:LegacyThumbprintsPath = Join-Path $Script:NanaZipDataRoot `
  'trusted-thumbprints.txt'
$Script:LegacyBackupDir = Join-Path $Script:NanaZipDataRoot 'cert-backup'
$Script:DownloadedPackagePath = $null
$Script:SignToolTempRoot = $null

Write-Host ('[ps] {0} {1} on {2}' -f
  $PSVersionTable.PSEdition, $PSVersionTable.PSVersion,
  [Environment]::OSVersion.VersionString) -ForegroundColor DarkGray

$Script:ErrorCodes = @{
  '0x800B0100' = 'the package is unsigned or its signature is invalid'
  '0x800B0109' = 'the chain ends in an untrusted root, run this once as admin'
  '0x800B010A' = 'no chain to a trusted root could be built'
  '0x800B0004' = 'the contents changed after signing, sign it again'
  '0x80073CF0' = 'the package could not be opened, rebuild and sign it'
  '0x80073CF3' = 'dependency or conflict validation failed'
  '0x80073CFB' = 'a package with this name already exists, uninstall it first'
  '0x80073CFF' = 'sideloading or developer mode is required'
  '0x80073D02' = 'the package is in use, close the app and its tasks'
  '0x80073D06' = 'version downgrade, install a newer build'
}

function Write-Step {
  param([string]$Message)
  Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Note {
  param([string]$Message)
  Write-Host "    $Message" -ForegroundColor DarkGray
}

function Write-Warn {
  param([string]$Message)
  Write-Host "    [!] $Message" -ForegroundColor Yellow
}

function Show-ErrorCode {
  param([string]$Code)
  if ($Script:ErrorCodes.ContainsKey($Code)) {
    Write-Host "  $Code  $($Script:ErrorCodes[$Code])" -ForegroundColor Yellow
  }
  else {
    Write-Host ("  $Code  not in the quick table, run Get-AppxLog" +
      ' or read AppxDeployment-Server event 404') -ForegroundColor Yellow
  }
}

function Test-IsAdmin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = New-Object Security.Principal.WindowsPrincipal $id
  $principal.IsInRole(
    [Security.Principal.WindowsBuiltinRole]::Administrator)
}

function Remove-StoreEntry {
  # Remove duplicate store entries for this thumbprint.
  param(
    [string]$Thumbprint,
    [string]$Store,
    [switch]$UserScope,
    [bool]$Apply = $true
  )

  $scope = if ($UserScope) { 'CurrentUser' } else { 'LocalMachine' }
  $path = "Cert:\$scope\$Store"
  # Keep the optional -user switch as an array for splatting.
  $prefix = @(if ($UserScope) { '-user' })

  for ($i = 0; $i -lt 10; $i++) {
    $hits = @(Get-ChildItem -LiteralPath $path -ErrorAction SilentlyContinue |
      Where-Object { $_.Thumbprint -eq $Thumbprint })
    if ($hits.Count -eq 0) { return }
    if (-not $Apply) {
      Write-Note "WhatIf: certutil -delstore $Store $Thumbprint"
      return
    }
    & certutil @prefix -delstore $Store $Thumbprint | Out-Null
    Start-Sleep -Milliseconds 200
  }
  Write-Warn "$path still holds $Thumbprint"
}

function Invoke-ProvisionedPackageCleanup {
  # Provisioning cmdlets require Windows PowerShell 5.1.
  param([string]$Name, [bool]$Apply = $true)

  $host5 = Join-Path $env:SystemRoot `
    'System32\WindowsPowerShell\v1.0\powershell.exe'
  if (-not (Test-Path -LiteralPath $host5)) {
    throw "Windows PowerShell 5.1 not found: $host5"
  }

  $helper = Join-Path ([IO.Path]::GetTempPath()) `
    ('NanaZipRevProvisioned-' + [guid]::NewGuid().ToString('N') + '.ps1')

  $body = @'
param([string]$Name, [switch]$Apply)
$ErrorActionPreference = 'Stop'
$found = @(Get-AppxProvisionedPackage -Online -ErrorAction Stop |
           Where-Object { $_.DisplayName -eq $Name })
foreach ($p in $found) {
  if ($Apply) {
    Remove-AppxProvisionedPackage -Online -PackageName $p.PackageName `
      -ErrorAction Stop | Out-Null
  }
  Write-Output ('HIT ' + $p.PackageName)
}
if ($found.Count -eq 0) { Write-Output 'NONE' }
'@

  Set-Content -LiteralPath $helper -Value $body -Encoding UTF8
  # Pass the switch only when needed; native arguments are strings.
  $childArgs = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
    '-File', $helper, '-Name', $Name)
  if ($Apply) { $childArgs += '-Apply' }
  try {
    $output = & $host5 @childArgs 2>&1
  }
  finally {
    Remove-Item -LiteralPath $helper -Force -ErrorAction SilentlyContinue
  }

  $hits = @()
  foreach ($line in @($output)) {
    $text = [string]$line
    if ($text -like 'HIT *') {
      $hits += $text.Substring(4).Trim()
    }
    elseif ($text -ne 'NONE' -and $text.Trim()) {
      Write-Note "[powershell 5.1] $text"
    }
  }
  return $hits
}

function Add-ZipTypes {
  # Load the ZIP assemblies available in this PowerShell edition.
  foreach ($assembly in 'System.IO.Compression',
                          'System.IO.Compression.FileSystem',
                          'System.IO.Compression.ZipFile') {
    if ('System.IO.Compression.ZipArchive' -as [type] -and
        'System.IO.Compression.ZipFileExtensions' -as [type]) {
      return
    }
    Add-Type -AssemblyName $assembly -ErrorAction SilentlyContinue
  }
  if (-not ('System.IO.Compression.ZipArchive' -as [type])) {
    throw 'System.IO.Compression is not available on this system'
  }
}

function Read-EntryText {
  # Read a ZIP entry as UTF-8 or UTF-16 text.
  param([string]$Archive, [string]$EntryName)

  Add-ZipTypes

  $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
  try {
    $entry = $zip.Entries |
      Where-Object FullName -eq $EntryName |
      Select-Object -First 1
    if (-not $entry) { return $null }
    $stream = $entry.Open()
    try {
      $memory = New-Object IO.MemoryStream
      $stream.CopyTo($memory)
      $bytes = $memory.ToArray()
    }
    finally { $stream.Dispose() }
  }
  finally { $zip.Dispose() }

  $isUtf16 = $bytes.Length -ge 2 -and
    (($bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) -or
     ($bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF))
  if ($isUtf16) {
    return [Text.Encoding]::Unicode.GetString($bytes)
  }
  $text = [Text.Encoding]::UTF8.GetString($bytes)
  return $text.TrimStart([char]0xFEFF)
}

function Get-PackagePublisher {
  # Read Publisher from the bundle manifest or package manifest.
  param([string]$Path)

  $candidates = 'AppxMetadata/AppxBundleManifest.xml', 'AppxManifest.xml'
  foreach ($entry in $candidates) {
    $text = Read-EntryText -Archive $Path -EntryName $entry
    if (-not $text) { continue }
    $pattern = 'Identity\b[^>]*?Publisher\s*=\s*"([^"]+)"'
    $match = [regex]::Match($text, $pattern)
    if ($match.Success) { return $match.Groups[1].Value }
  }
  return $null
}

function Select-ReleaseBundle {
  # Require exactly one bundle asset.
  param([object[]]$Assets)

  $bundles = @($Assets | Where-Object { $_.name -like '*.msixbundle' })
  if ($bundles.Count -eq 0) { return $null }
  if ($bundles.Count -gt 1) {
    $names = ($bundles | ForEach-Object { $_.name }) -join ', '
    throw ("the release carries $($bundles.Count) msixbundle assets " +
      "($names), there should be exactly one")
  }
  return $bundles[0]
}

function Save-RemotePackage {
  # Download and verify the latest release bundle.
  param([string]$Owner)

  $headers = @{
    'User-Agent' = 'Install.ps1'
    'Accept' = 'application/vnd.github+json'
  }
  if ($env:GITHUB_TOKEN) {
    $headers.Authorization = "Bearer $env:GITHUB_TOKEN"
  }

  $uri = "https://api.github.com/repos/$Owner/releases/latest"
  $proxyArg = if ($Proxy) { @{ Proxy = $Proxy } } else { @{} }
  try {
    $release = Invoke-RestMethod -Uri $uri -Headers $headers -ErrorAction Stop @proxyArg
  }
  catch {
    Write-Note "no release on $Owner ($($_.Exception.Message))"
    return $null
  }

  $asset = Select-ReleaseBundle -Assets @($release.assets)
  if (-not $asset) {
    Write-Note "release $($release.tag_name) has no msixbundle"
    return $null
  }

  if ($asset.digest -notmatch '^sha256:([0-9a-fA-F]{64})$') {
    throw "release asset $($asset.name) has no usable GitHub SHA-256 digest; refusing download"
  }
  $expectedHash = $Matches[1].ToUpperInvariant()
  if ($asset.name -match '[\\/]' -or $asset.name -match '(^|[.])\.\.([.]|$)') {
    throw "unsafe release asset filename: $($asset.name)"
  }
  $downloadUri = $null
  if (-not [Uri]::TryCreate([string]$asset.browser_download_url,
      [UriKind]::Absolute, [ref]$downloadUri) -or
      $downloadUri.Scheme -ne 'https' -or $downloadUri.Host -ne 'github.com' -or
      -not $downloadUri.AbsolutePath.StartsWith('/' + $Owner.Trim('/') + '/releases/download/',
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "unexpected GitHub release asset URL: $($asset.browser_download_url)"
  }

  $cache = Join-Path ([IO.Path]::GetTempPath()) `
    ('NanaZipRev-' + [guid]::NewGuid().ToString('N') + '.msixbundle')
  Write-Note "downloading $($release.tag_name) / $($asset.name)"
  try {
    Invoke-WebRequest -Uri $downloadUri.AbsoluteUri -OutFile $cache `
      -UseBasicParsing -Headers @{ 'User-Agent' = 'Install.ps1' } @proxyArg
    $actualHash = (Get-FileHash -LiteralPath $cache -Algorithm SHA256).Hash
    if ($actualHash -cne $expectedHash) {
      throw "GitHub release SHA-256 mismatch for $($asset.name); refusing installation"
    }
  }
  catch {
    Remove-Item -LiteralPath $cache -Force -ErrorAction SilentlyContinue
    throw
  }
  $Script:DownloadedPackagePath = $cache
  Write-Note "verified SHA-256 $actualHash"
  return $cache
}

function Resolve-PackagePath {
  if ($LocalPath) {
    return (Resolve-Path -LiteralPath $LocalPath -ErrorAction Stop).ProviderPath
  }

  if ($env:NANAZIPREV_MSIX) {
    Write-Note 'using NANAZIPREV_MSIX'
    $fromEnv = Resolve-Path -LiteralPath $env:NANAZIPREV_MSIX -ErrorAction Stop
    return $fromEnv.ProviderPath
  }

  Write-Step "looking for the latest release on $Repo"
  $remote = Save-RemotePackage -Owner $Repo
  if ($remote) { return $remote }

  if (-not $LocalSearchRoot) {
    throw "no release on $Repo, build first or pass -LocalPath"
  }

  Write-Note "falling back to the build output under $LocalSearchRoot"
  $local = Get-ChildItem -LiteralPath $LocalSearchRoot -Recurse `
    -Filter '*.msixbundle' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if (-not $local) {
    throw "no release on $Repo and no package under " +
      "$LocalSearchRoot, build first or pass -LocalPath"
  }
  Write-Note "found $($local.FullName)"
  return $local.FullName
}

function Download-Bytes {
  param([string]$Uri)

  # Read the response into memory; do not create an intermediate file.
  $proxyArg = if ($Proxy) { @{ Proxy = $Proxy } } else { @{} }
  $response = Invoke-WebRequest -Uri $Uri -Headers @{
    'User-Agent' = 'Install.ps1'
  } -UseBasicParsing -ErrorAction Stop @proxyArg
  $stream = $response.RawContentStream
  if (-not $stream) { throw "download returned no response stream: $Uri" }
  $memory = New-Object IO.MemoryStream
  try {
    $stream.CopyTo($memory)
    return ,$memory.ToArray()
  }
  finally {
    $memory.Dispose()
    $stream.Dispose()
  }
}

function Get-SignToolArchOrder {
  # Prefer the native architecture before emulation targets.
  switch ($env:PROCESSOR_ARCHITECTURE) {
    'ARM64' { return 'arm64', 'x64', 'x86' }
    'x86' { return 'x86', 'x64', 'arm64' }
    default { return 'x64', 'x86', 'arm64' }
  }
}

function Select-SignTool {
  param([object[]]$Files)

  foreach ($arch in (Get-SignToolArchOrder)) {
    $hit = $Files |
      Where-Object { $_.Directory.Name -eq $arch } |
      Select-Object -First 1
    if ($hit) { return $hit.FullName }
  }
  return $null
}

function Get-SignToolIn {
  param([string]$Root, [switch]$Recurse)

  if (-not (Test-Path -LiteralPath $Root)) { return $null }
  $rootItem = Get-Item -LiteralPath $Root -Force -ErrorAction Stop
  if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "refusing to search a reparse-point directory: $Root"
  }
  $find = @{
    LiteralPath = $Root
    Filter = 'signtool.exe'
    ErrorAction = 'SilentlyContinue'
  }
  if ($Recurse) { $find.Recurse = $true }
  $files = @(Get-ChildItem @find)
  return Select-SignTool -Files $files
}

function Test-MicrosoftSignedBinary {
  param([string]$Path)

  try { $signature = Get-AuthenticodeSignature -FilePath $Path -ErrorAction Stop }
  catch { return $false }
  return ($signature.Status -eq 'Valid' -and
    $signature.SignerCertificate -and
    $signature.SignerCertificate.Subject -match 'CN=Microsoft Corporation')
}

function Get-Sha512Base64FromBytes {
  param([byte[]]$Bytes)

  $sha512 = [Security.Cryptography.SHA512]::Create()
  try { return [Convert]::ToBase64String($sha512.ComputeHash($Bytes)) }
  finally { $sha512.Dispose() }
}

function Get-NuGetPackageHash {
  param(
    [Parameter(Mandatory = $true)][string]$PackageId,
    [Parameter(Mandatory = $true)][string]$Version
  )

  $headers = @{ 'User-Agent' = 'Install.ps1'; 'Accept' = 'application/json' }
  $proxyArg = if ($Proxy) { @{ Proxy = $Proxy } } else { @{} }
  $serviceIndex = Invoke-RestMethod -Uri 'https://api.nuget.org/v3/index.json' `
    -Headers $headers -ErrorAction Stop @proxyArg
  $registration = $serviceIndex.resources | Where-Object {
    @($_.'@type') -contains 'RegistrationsBaseUrl'
  } | Select-Object -First 1
  if (-not $registration) {
    throw 'NuGet service index has no registration resource'
  }

  $registrationBase = [string]$registration.'@id'
  if (-not $registrationBase.EndsWith('/')) { $registrationBase += '/' }
  $lowerId = $PackageId.ToLowerInvariant()
  $indexUri = [Uri]::new([Uri]$registrationBase, "$lowerId/index.json").AbsoluteUri
  $index = Invoke-RestMethod -Uri $indexUri -Headers $headers `
    -ErrorAction Stop @proxyArg
  $leaf = $null

  foreach ($page in @($index.items)) {
    $leaves = if ($page.PSObject.Properties['items']) { @($page.items) } else { @() }
    if ($leaves.Count -eq 0) {
      $pageUri = [string]$page.'@id'
      if (-not $pageUri) { throw "NuGet registration page for $PackageId has no URL" }
      $pageDocument = Invoke-RestMethod -Uri $pageUri -Headers $headers `
        -ErrorAction Stop @proxyArg
      $leaves = @($pageDocument.items)
    }

    $leaf = $leaves | Where-Object {
      [string]$_.catalogEntry.version -ieq $Version
    } | Select-Object -First 1
    if ($leaf) { break }
  }

  if (-not $leaf) {
    throw "NuGet registration has no exact version $PackageId $Version"
  }

  $catalogUri = [string]$leaf.catalogEntry.'@id'
  $catalogUriObject = $null
  if (-not [Uri]::TryCreate($catalogUri, [UriKind]::Absolute, [ref]$catalogUriObject) -or
      $catalogUriObject.Scheme -ne 'https' -or
      $catalogUriObject.Host -ne 'api.nuget.org') {
    throw "unexpected NuGet catalog URL: $catalogUri"
  }
  $catalog = Invoke-RestMethod -Uri $catalogUriObject.AbsoluteUri `
    -Headers $headers -ErrorAction Stop @proxyArg

  if ($catalog.id -ine $PackageId -or $catalog.version -ine $Version) {
    throw "NuGet catalog entry does not match $PackageId $Version"
  }
  if ($catalog.packageHashAlgorithm -ine 'SHA512') {
    throw "NuGet catalog did not report SHA512 for $PackageId $Version"
  }

  $packageHash = [string]$catalog.packageHash
  $hashBytes = [Convert]::FromBase64String($packageHash)
  if ($hashBytes.Length -ne 64) {
    throw "NuGet packageHash for $PackageId $Version is not a SHA-512 digest"
  }
  return $packageHash
}

function Remove-TemporarySignToolDirectory {
  param([string]$Root)

  if (-not $Root) { return }
  try {
    $tempPath = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
      [char[]]@('\', '/'))
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd([char[]]@('\', '/'))
    $parent = [IO.Path]::GetDirectoryName($fullRoot).TrimEnd([char[]]@('\', '/'))
    $leaf = [IO.Path]::GetFileName($fullRoot)
    if (-not [StringComparer]::OrdinalIgnoreCase.Equals($parent, $tempPath) -or
        $leaf -notmatch '^NanaZipRev-SignTool-[0-9a-f]{32}$') {
      Write-Warn "refusing to remove unexpected temporary SignTool path: $Root"
      return
    }
    if (-not (Test-Path -LiteralPath $fullRoot)) { return }
    $rootItem = Get-Item -LiteralPath $fullRoot -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer -or
        ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      Write-Warn "refusing to remove an unsafe temporary SignTool path: $fullRoot"
      return
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $fullRoot -Force -Recurse -ErrorAction Stop)) {
      if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        Write-Warn "refusing to recursively remove a temporary SignTool tree containing a reparse point: $($item.FullName)"
        return
      }
    }
    Remove-Item -LiteralPath $fullRoot -Recurse -Force -ErrorAction Stop
  }
  catch {
    Write-Warn "could not clean up temporary SignTool files: $($_.Exception.Message)"
  }
}

function Find-SignTool {
  # Prefer a Microsoft-signed SignTool on PATH; do not reuse temporary tools.
  $pathCandidates = @(Get-Command 'signtool.exe' -CommandType Application -All `
    -ErrorAction SilentlyContinue)
  foreach ($candidate in $pathCandidates) {
    if (Test-MicrosoftSignedBinary -Path $candidate.Source) {
      Write-Note "using Microsoft-signed signtool from PATH: $($candidate.Source)"
      return $candidate.Source
    }
    Write-Warn "ignoring signtool on PATH because its Microsoft Authenticode signature is not valid: $($candidate.Source)"
  }

  Write-Step 'no trusted signtool.exe on PATH, downloading the Windows SDK build tools'
  $version = '10.0.26100.9169'
  $id = 'microsoft.windows.sdk.buildtools'
  $uri = "https://api.nuget.org/v3-flatcontainer/$id/$version" +
    "/$id.$version.nupkg"

  # The package hash comes from NuGet's Catalog PackageDetails document.
  $expectedHash = Get-NuGetPackageHash -PackageId $id -Version $version
  $bytes = Download-Bytes -Uri $uri
  if ((Get-Sha512Base64FromBytes -Bytes $bytes) -cne $expectedHash) {
    throw 'Windows SDK NuGet package SHA-512 mismatch; refusing to extract it'
  }

  # Use a fresh directory for this run; the caller removes it after signing.
  $tempPath = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd(
    [char[]]@('\', '/'))
  $toolRoot = Join-Path $tempPath (
    'NanaZipRev-SignTool-' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $toolRoot -ErrorAction Stop | Out-Null
  $Script:SignToolTempRoot = $toolRoot

  try {
    $rootItem = Get-Item -LiteralPath $toolRoot -Force -ErrorAction Stop
    if (-not $rootItem.PSIsContainer -or
        ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
      throw "refusing to extract the SDK into an unsafe temporary path: $toolRoot"
    }

    Add-ZipTypes
    $memory = [IO.MemoryStream]::new($bytes)
    try {
      $zip = [IO.Compression.ZipArchive]::new(
        $memory, [IO.Compression.ZipArchiveMode]::Read)
      try {
        # Extract only the tool and adjacent DLLs; reject traversal and duplicates.
        $pattern = '^bin/(?!\.{1,2}/)[^/]+/(x86|x64|arm64)/(signtool\.exe|[^/]+\.dll)$'
        $rootPrefix = [IO.Path]::GetFullPath($toolRoot).TrimEnd(
          [char[]]@('\', '/')) + '\'
        $seen = @{}
        foreach ($entry in $zip.Entries) {
          if ($entry.FullName -notmatch $pattern) { continue }
          $relative = $entry.FullName -replace '/', '\'
          $target = [IO.Path]::GetFullPath((Join-Path $toolRoot $relative))
          if (-not $target.StartsWith($rootPrefix,
              [StringComparison]::OrdinalIgnoreCase)) {
            throw "SDK archive entry escapes the temporary directory: $($entry.FullName)"
          }
          if ($seen.ContainsKey($target)) {
            throw "SDK archive contains a duplicate tool entry: $($entry.FullName)"
          }
          $seen[$target] = $true
          $parent = Split-Path -Parent $target
          New-Item -ItemType Directory -Force -Path $parent -ErrorAction Stop | Out-Null
          [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
        }
      }
      finally { $zip.Dispose() }
    }
    finally { $memory.Dispose() }

    $hit = Get-SignToolIn -Root $toolRoot -Recurse
    if (-not $hit) {
      throw 'the SDK package carried no compatible signtool.exe'
    }
    if (-not (Test-MicrosoftSignedBinary -Path $hit)) {
      throw "extracted signtool.exe does not have a valid Microsoft Authenticode signature: $hit"
    }
    Write-Note "downloaded and extracted Microsoft-signed signtool: $hit"
    return $hit
  }
  catch {
    Remove-TemporarySignToolDirectory -Root $toolRoot
    $Script:SignToolTempRoot = $null
    throw
  }
}

function Get-OrCreateCertificate {
  param([string]$Subject, [bool]$Apply = $true)

  $existing = Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' `
    -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $Subject -and $_.HasPrivateKey } |
    Select-Object -First 1
  if ($existing) {
    Write-Step "reusing certificate $($existing.Thumbprint)"
    return $existing
  }

  if (-not $Apply) {
    Write-Step "would create a self-signed certificate for $Subject"
    return $null
  }

  Write-Step "creating a self-signed code-signing certificate for $Subject"
  $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -KeyAlgorithm RSA -KeyLength 2048 `
    -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears($Years) `
    -FriendlyName 'NanaZip Rev local test'

  # The private key is never exported.
  return $cert
}

function Trust-Certificate {
  <#
    The machine context needs an elevated shell. Import-Certificate refuses
    the user root store with "UI is not allowed", so certutil is the only way
    in.
  #>
  param(
    [Security.Cryptography.X509Certificates.X509Certificate2]$Certificate,
    [bool]$Apply = $true
  )

  if (-not $Apply) {
    Write-Step "would trust $($Certificate.Thumbprint) in CurrentUser\Root"
    if (Test-IsAdmin) {
      Write-Step 'would also trust it in LocalMachine and TrustedPeople'
    }
    return
  }

  $cer = Join-Path ([IO.Path]::GetTempPath()) `
    ('NanaZipRevTrust-' + $Certificate.Thumbprint + '.cer')
  Export-Certificate -Cert $Certificate -FilePath $cer | Out-Null
  try {
    & certutil -user -addstore Root $cer | Out-Null
    if ($LASTEXITCODE -ne 0) {
      throw "certutil failed to trust $($Certificate.Thumbprint) in CurrentUser\Root"
    }
    if (Test-IsAdmin) {
      & certutil -addstore Root $cer | Out-Null
      if ($LASTEXITCODE -ne 0) {
        throw "certutil failed to trust $($Certificate.Thumbprint) in LocalMachine\Root"
      }
      & certutil -addstore TrustedPeople $cer | Out-Null
      if ($LASTEXITCODE -ne 0) {
        throw "certutil failed to trust $($Certificate.Thumbprint) in LocalMachine\TrustedPeople"
      }
      Write-Step 'trusted in CurrentUser, LocalMachine and TrustedPeople'
    }
    else {
      Write-Step 'trusted in Cert:\CurrentUser\Root'
      Write-Warn ('not elevated, run this once as admin if the install ' +
        'fails with 0x800B0109')
    }
  }
  finally { Remove-Item -LiteralPath $cer -Force -ErrorAction SilentlyContinue }
}

function Remove-LegacyInstallerData {
  param([bool]$Apply = $true)

  $thumbs = @()
  if (Test-Path -LiteralPath $Script:LegacyThumbprintsPath) {
    $thumbFile = Get-Item -LiteralPath $Script:LegacyThumbprintsPath -Force `
      -ErrorAction Stop
    if ($thumbFile.Attributes -band [IO.FileAttributes]::ReparsePoint) {
      throw "refusing to read legacy thumbprint list through a reparse point: $Script:LegacyThumbprintsPath"
    }
    $thumbs = @(Get-Content -LiteralPath $Script:LegacyThumbprintsPath |
      Where-Object { $_ -match '^[0-9A-Fa-f]{40}$' } |
      Sort-Object -Unique)
  }

  foreach ($thumbprint in $thumbs) {
    $stores = @('Cert:\CurrentUser\My', 'Cert:\CurrentUser\Root')
    if (Test-IsAdmin) {
      $stores += 'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPeople'
    }
    foreach ($store in $stores) {
      $hits = @(Get-ChildItem -LiteralPath $store -ErrorAction SilentlyContinue |
        Where-Object { $_.Thumbprint -eq $thumbprint })
      if ($hits.Count -eq 0) { continue }
      $userScope = $store.StartsWith('Cert:\CurrentUser\',
        [StringComparison]::OrdinalIgnoreCase)
      $storeName = $store.Substring($store.LastIndexOf('\') + 1)
      Remove-StoreEntry -Thumbprint $thumbprint -Store $storeName `
        -UserScope:$userScope -Apply $Apply
    }
  }

  if (Test-Path -LiteralPath $Script:LegacyThumbprintsPath) {
    Write-Step "deleting legacy thumbprint list $Script:LegacyThumbprintsPath"
    if (Invoke-ShouldProcess $Script:LegacyThumbprintsPath 'Remove-Item') {
      Remove-Item -LiteralPath $Script:LegacyThumbprintsPath -Force `
        -ErrorAction SilentlyContinue
    }
  }

  if (Test-Path -LiteralPath $Script:LegacyBackupDir) {
    $backupRoot = [IO.Path]::GetFullPath($Script:NanaZipDataRoot).TrimEnd(
      [char[]]@('\\', '/')) + '\\'
    $backupDir = [IO.Path]::GetFullPath($Script:LegacyBackupDir)
    if (-not $backupDir.StartsWith($backupRoot,
        [StringComparison]::OrdinalIgnoreCase)) {
      throw "refusing to remove legacy backup outside ${Script:NanaZipDataRoot}: $backupDir"
    }
    $current = $backupDir
    while ($true) {
      if (Test-Path -LiteralPath $current) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
          throw "refusing recursive deletion through a reparse point: $current"
        }
      }
      if ($current.Equals($backupRoot.TrimEnd('\\'),
          [StringComparison]::OrdinalIgnoreCase)) { break }
      $parent = Split-Path -Parent $current
      if (-not $parent -or $parent -eq $current -or
          -not $current.StartsWith($backupRoot,
            [StringComparison]::OrdinalIgnoreCase)) { break }
      $current = $parent
    }
    Write-Step "deleting legacy certificate backup $backupDir"
    if (Invoke-ShouldProcess $backupDir 'Remove-Item') {
      Remove-Item -LiteralPath $backupDir -Recurse -Force `
        -ErrorAction SilentlyContinue
    }
  }
}

function Remove-TrustedCertificate {
  param([string]$Subject, [bool]$Apply = $true)

  $certs = @(Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' `
      -ErrorAction SilentlyContinue | Where-Object {
        $_.Subject -eq $Subject -and $_.HasPrivateKey -and
        $_.FriendlyName -eq 'NanaZip Rev local test'
      })

  foreach ($cert in $certs) {
    Write-Step "removing certificate $($cert.Thumbprint)"
    Remove-StoreEntry -Thumbprint $cert.Thumbprint -Store 'My' `
      -UserScope -Apply $Apply
    Remove-StoreEntry -Thumbprint $cert.Thumbprint -Store 'Root' `
      -UserScope -Apply $Apply
    if (Test-IsAdmin) {
      Remove-StoreEntry -Thumbprint $cert.Thumbprint -Store 'Root' `
        -Apply $Apply
      Remove-StoreEntry -Thumbprint $cert.Thumbprint -Store 'TrustedPeople' `
        -Apply $Apply
    }
  }
}
function Stop-PackageProcesses {
  # Stop only processes launched from this package's install directory.
  param([string]$InstallLocation)

  if (-not $InstallLocation) { return 0 }
  if (-not (Test-Path -LiteralPath $InstallLocation)) { return 0 }
  $installPrefix = $InstallLocation.TrimEnd([char[]]@('\', '/')) + '\'
  try {
    $running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
      $_.Path -and
      $_.Path.StartsWith($installPrefix,
        [StringComparison]::OrdinalIgnoreCase)
    })
  }
  catch { $running = @() }

  $stopped = 0
  foreach ($process in $running) {
    Write-Host "    stopping $($process.ProcessName) (pid $($process.Id))"
    try {
      Stop-Process -Id $process.Id -Force -ErrorAction Stop
      $stopped++
    }
    catch { }
  }
  return $stopped
}

function Remove-PackageWithTimeout {
  # Report progress and time out if package removal stalls.
  param([string]$PackageFullName)

  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  try {
    $job = Start-Job -ErrorAction Stop -ArgumentList $PackageFullName `
      -ScriptBlock {
        param($name)
        if ($PSVersionTable.PSEdition -eq 'Core') {
          Import-Module Appx -UseWindowsPowerShell -ErrorAction Stop
        }
        Remove-AppxPackage -Package $name -ErrorAction Stop
      }
  }
  catch {
    Write-Note 'no background job available, removing in the foreground'
    Remove-AppxPackage -Package $PackageFullName -ErrorAction Stop
    return
  }

  while ($job.State -in 'Running', 'NotStarted') {
    Start-Sleep -Seconds 5
    Write-Host ("    ... {0:N0}s" -f $stopwatch.Elapsed.TotalSeconds)
    if ($stopwatch.Elapsed.TotalSeconds -ge $RemovalTimeoutSeconds) {
      Stop-Job $job -ErrorAction SilentlyContinue
      Remove-Job $job -Force -ErrorAction SilentlyContinue
      throw 'removing the package did not finish in time, close every ' +
        "NanaZip window and retry"
    }
  }

  $jobError = $null
  try { $jobError = $job.ChildJobs[0].Error } catch { }
  Remove-Job $job -Force -ErrorAction SilentlyContinue
  if ($jobError) {
    Write-Warn 'the background job failed, removing in the foreground'
    Remove-AppxPackage -Package $PackageFullName -ErrorAction SilentlyContinue
    if (Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue) {
      $why = ($jobError | Out-String).Trim()
      throw "Remove-AppxPackage failed: $why"
    }
    return
  }
  Write-Host ("    took {0:N0}s" -f $stopwatch.Elapsed.TotalSeconds)
}

function Install-Package {
  param([string]$Path)

  $subject = Get-PackagePublisher -Path $Path
  if (-not $subject) {
    throw "could not read the package Publisher; expected exactly '$Publisher'"
  }
  if ($subject -cne $Publisher) {
    throw "package Publisher mismatch: manifest='$subject'; expected='$Publisher'"
  }

  $signature = Get-AuthenticodeSignature -FilePath $Path
  if ($signature.Status -eq 'Valid') {
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -cne $Publisher) {
      $signerSubject = if ($signature.SignerCertificate) {
        $signature.SignerCertificate.Subject
      } else { '<missing signer certificate>' }
      throw "signature signer mismatch: signer='$signerSubject'; expected='$Publisher'"
    }
  }

  if ($NoSign) {
    Write-Step "signing skipped, current status $($signature.Status)"
  }
  elseif ($signature.Status -eq 'Valid') {
    Write-Step "already signed by $($signature.SignerCertificate.Subject)"
    # Trust only the exact verified signer certificate.
    Trust-Certificate -Certificate $signature.SignerCertificate `
      -Apply:(-not $WhatIf)
  }
  else {
    Write-Step 'package is unsigned; signing it'
    if (-not $WhatIf) {
      $signTool = Find-SignTool
      Write-Note "signtool $signTool"
    }

    $certificate = Get-OrCreateCertificate -Subject $subject `
      -Apply:(-not $WhatIf)
    if (-not $certificate) { return }
    Trust-Certificate -Certificate $certificate -Apply:(-not $WhatIf)
    if ($WhatIf) {
      Write-Host 'WhatIf: the package would be signed and installed.' `
        -ForegroundColor DarkGray
      return
    }
    # Use DigiCert's HTTP RFC 3161 endpoint with this SignTool version.
    $stamp = 'http://timestamp.digicert.com'
    & $signTool sign /fd SHA256 /td SHA256 /tr $stamp `
      /sha1 $certificate.Thumbprint $Path
    if ($LASTEXITCODE -ne 0) {
      throw "signtool failed with exit code $LASTEXITCODE using the HTTP RFC 3161 timestamp service"
    }
    & $signTool verify /pa $Path | Out-Null
    if ($LASTEXITCODE -ne 0) {
      throw 'the signature does not verify after signing'
    }
    Write-Step 'signed and verified'
  }

  $existing = Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue
  if ($existing -and -not $ForceUpdateFromAnyVersion) {
    Write-Step "removing $($existing.PackageFullName)"
    $target = $existing.PackageFullName
    if (Invoke-ShouldProcess $target 'Remove-AppxPackage') {
      $location = $existing.InstallLocation
      $stopped = Stop-PackageProcesses -InstallLocation $location
      if ($stopped -gt 0) {
        Write-Host "    stopped $stopped process(es) holding package files"
      }
      Remove-PackageWithTimeout -PackageFullName $target
      Write-Step 'old package removed'
    }
    else {
      Write-Host 'WhatIf: the old package stays.' -ForegroundColor DarkGray
    }
  }
  elseif ($existing) {
    Write-Step "updating $($existing.PackageFullName) in place"
  }

  Write-Step 'installing'
  $arguments = @{ Path = $Path; ErrorAction = 'Stop' }
  if ($ForceUpdateFromAnyVersion) {
    $arguments.ForceUpdateFromAnyVersion = $true
  }
  if (Invoke-ShouldProcess $Path 'Add-AppxPackage') {
    try {
      Add-AppxPackage @arguments
    }
    catch {
      Write-Host ''
      Write-Host "install failed: $($_.Exception.Message)" -ForegroundColor Red
      $code = [regex]::Match($_.Exception.Message, '0x[0-9A-Fa-f]{8}')
      if ($code.Success) { Show-ErrorCode -Code $code.Value }
      throw
    }
  }
}

function Uninstall-Package {
  $packages = @(Get-AppxPackage -Name $PackageName `
    -ErrorAction SilentlyContinue)
  if ($packages.Count -eq 0) { Write-Step 'nothing installed' }

  foreach ($package in $packages) {
    Write-Step "removing $($package.PackageFullName)"
    $target = $package.PackageFullName
    if (-not (Invoke-ShouldProcess $target 'Remove-AppxPackage')) {
      continue
    }
    $stopped = Stop-PackageProcesses -InstallLocation $package.InstallLocation
    if ($stopped -gt 0) {
      Write-Host "    stopped $stopped process(es) holding package files"
    }
    if (Test-IsAdmin) {
      Remove-AppxPackage -Package $target -AllUsers -ErrorAction Stop | Out-Null
    }
    else {
      Remove-AppxPackage -Package $target -ErrorAction Stop | Out-Null
    }
  }

  if (Test-IsAdmin) {
    $apply = -not $WhatIf
    foreach ($name in @(Invoke-ProvisionedPackageCleanup -Name $PackageName `
        -Apply $apply)) {
      Write-Note "provisioned package removed: $name"
    }
  }

  Remove-TrustedCertificate -Subject $Publisher -Apply:(-not $WhatIf)

  Write-Host ''
  if (Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue) {
    Write-Warn "$PackageName is still installed"
  }
  else {
    Write-Host "OK  $PackageName is gone" -ForegroundColor Green
  }
}

Remove-LegacyInstallerData -Apply:(-not $WhatIf)

if ($Uninstall) {
  Uninstall-Package
  return
}

$package = Resolve-PackagePath
Write-Step "package : $package"
try {
  Install-Package -Path $package
}
finally {
  if ($Script:DownloadedPackagePath -and
      (Test-Path -LiteralPath $Script:DownloadedPackagePath)) {
    Remove-Item -LiteralPath $Script:DownloadedPackagePath -Force `
      -ErrorAction SilentlyContinue
  }
  if ($Script:SignToolTempRoot) {
    Remove-TemporarySignToolDirectory -Root $Script:SignToolTempRoot
    $Script:SignToolTempRoot = $null
  }
}

if ($WhatIf) {
  Write-Host ''
  Write-Host 'WhatIf: nothing was installed or removed.' `
    -ForegroundColor DarkGray
  return
}

$installed = Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue
if (-not $installed) {
  Write-Warn 'Add-AppxPackage reported success but nothing is listed'
  return
}

Write-Host ''
Write-Host "OK  $($installed.PackageFullName)" -ForegroundColor Green
Write-Host "    install location : $($installed.InstallLocation)"
$exe = Join-Path $installed.InstallLocation 'NanaZip.Modern.FileManager.exe'
if (Test-Path -LiteralPath $exe) { Write-Host "    gui              : $exe" }
$launch = "explorer.exe shell:AppsFolder\$($installed.PackageFamilyName)!App"
Write-Host "    launch           : $launch"
