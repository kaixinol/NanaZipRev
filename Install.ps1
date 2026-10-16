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
  certificate and the certificate backup, so nothing is left behind.

  A published package carries no signature, so one is made on the machine,
  the package is signed with it and it is trusted. A package that already
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

.PARAMETER BackupDir
  Where the generated certificate and its pfx are kept.

.PARAMETER PfxPassword
  Protects the generated pfx. It is a local test certificate, so this is not a
  secret worth guarding.

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
  Remove the package, its certificate and the backup.
  .\Install.ps1 -Uninstall

.NOTES
  Deploying a package this script signed itself needs sideloading, so either
  developer mode or the Allow sideloading of apps policy. Runs on Windows
  PowerShell 5.1 and on PowerShell 7; only the provisioned package step needs
  5.1, and that step is handed over to it when it is needed.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$LocalPath,
  [switch]$Uninstall,
  [switch]$NoSign,
  [switch]$ForceUpdateFromAnyVersion,
  [string]$PackageName = 'Kaesinol.NanaZipRev',
  [string]$Publisher = 'CN=23EBD860-A383-4AEA-836A-B041352C93FF',
  [string]$Repo = 'kaixinol/NanaZipRev',
  [string]$LocalSearchRoot,
  [string]$BackupDir,
  [string]$PfxPassword = 'NanaZipRev-Test',
  [int]$Years = 5,
  [int]$RemovalTimeoutSeconds = 180
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol =
  [Net.SecurityProtocolType]::Tls12

if (-not $LocalSearchRoot) {
  $LocalSearchRoot = Join-Path $PSScriptRoot 'Output\Binaries\AppPackages'
}
if (-not $BackupDir) {
  $BackupDir = Join-Path $env:LOCALAPPDATA 'NanaZipRev\cert-backup'
}

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
  <#
    certutil is the only way into CurrentUser\Root, and one thumbprint can sit
    in a store twice, once as the certificate and once as a public key only.
  #>
  param(
    [string]$Thumbprint,
    [string]$Store,
    [switch]$UserScope,
    [bool]$Apply = $true
  )

  $scope = if ($UserScope) { 'CurrentUser' } else { 'LocalMachine' }
  $path = "Cert:\$scope\$Store"
  $prefix = if ($UserScope) { @('-user') } else { @() }

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
  <#
    Removes the copy that is provisioned for future accounts. The cmdlets work
    on 5.1 only; on 7 they are found but fail with "Class not registered".
  #>
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
  # A switch on a native command line arrives as a string, which a
  # SwitchParameter refuses to convert.
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
  <# The two PowerShell editions keep the zip types in different assemblies. #>
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
  <# Reads one text entry out of a zip, UTF-8 or UTF-16. #>
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
  <#
    The signing tool rejects a certificate whose subject is not the publisher
    the manifest declares. A bundle keeps it in AppxBundleManifest.xml and a
    single package in AppxManifest.xml.
  #>
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
  <#
    The one msixbundle among the assets of a release. More than one is an
    error, because then there is no telling which one is meant.
  #>
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
  <#
    The one msixbundle the latest release carries, downloaded to the temp
    directory. Returns nothing when there is no release, no msixbundle, or
    the API refuses the call, and fails when the release carries more than
    one.
  #>
  param([string]$Owner)

  $headers = @{
    'User-Agent' = 'Install.ps1'
    'Accept' = 'application/vnd.github+json'
  }
  if ($env:GITHUB_TOKEN) {
    $headers.Authorization = "Bearer $env:GITHUB_TOKEN"
  }

  $uri = "https://api.github.com/repos/$Owner/releases/latest"
  try {
    $release = Invoke-RestMethod -Uri $uri -Headers $headers -ErrorAction Stop
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

  $cache = Join-Path ([IO.Path]::GetTempPath()) ('NanaZipRev-' + $asset.name)
  Write-Note "downloading $($release.tag_name) / $($asset.name)"
  Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $cache `
    -UseBasicParsing -Headers @{ 'User-Agent' = 'Install.ps1' }
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
  <#
    Bytes without touching the disk. Invoke-WebRequest either needs a file or
    decodes the response into text, and the bytes are lost either way.
  #>
  param([string]$Uri)

  $client = New-Object Net.WebClient
  $client.Headers.Add('User-Agent', 'Install.ps1')
  try { return $client.DownloadData($Uri) }
  finally { $client.Dispose() }
}

function Get-SignToolArchOrder {
  <#
    The architecture this process runs as comes first. An x64 build runs
    nowhere else, while x64 and x86 both run on arm64 through emulation.
  #>
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
  $find = @{
    LiteralPath = $Root
    Filter = 'signtool.exe'
    ErrorAction = 'SilentlyContinue'
  }
  if ($Recurse) { $find.Recurse = $true }
  $files = @(Get-ChildItem @find)
  return Select-SignTool -Files $files
}

function Find-SignToolInTempRoot {
  <#
    Some other tool may have dropped a signing kit straight into the temp
    directory. The root only: a recursive walk of it is not worth the time,
    and there is no architecture directory to key on at that level.
  #>
  $hit = Get-ChildItem -LiteralPath ([IO.Path]::GetTempPath()) `
    -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($hit) { return $hit.FullName }
  return $null
}

function Find-SignTool {
  $onPath = Get-Command 'signtool.exe' -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($onPath) {
    Write-Note "signtool on PATH: $($onPath.Source)"
    return $onPath.Source
  }

  $kits = @(
    (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'),
    (Join-Path $env:ProgramFiles 'Windows Kits\10\bin')
  )
  foreach ($root in $kits) {
    $hit = Get-SignToolIn -Root $root -Recurse
    if ($hit) { return $hit }
  }

  $temp = [IO.Path]::GetTempPath()
  $cache = Join-Path $temp 'NanaZipRev\winsdk'
  $hit = Get-SignToolIn -Root $cache -Recurse
  if ($hit) { return $hit }

  # Some other tool may have dropped a signing kit straight into the temp
  # directory.
  $hit = Find-SignToolInTempRoot
  if ($hit) {
    Write-Note "signtool found in $([IO.Path]::GetTempPath())"
    return $hit
  }

  Write-Step 'signtool.exe not found, fetching the Windows SDK build tools'
  $version = '10.0.26100.9169'
  $id = 'microsoft.windows.sdk.buildtools'
  $uri = "https://api.nuget.org/v3-flatcontainer/$id/$version" +
    "/$id.$version.nupkg"

  # Kept in memory: 20 MB of bytes, and only the signing tools are wanted.
  $bytes = Download-Bytes -Uri $uri

  Add-ZipTypes
  $memory = [IO.MemoryStream]::new($bytes)
  try {
    $zip = [IO.Compression.ZipArchive]::new(
      $memory, [IO.Compression.ZipArchiveMode]::Read)
    try {
      $pattern = '^(bin/[^/]+/)(x64|arm64)/(signtool\.exe|.*\.dll)$'
      foreach ($entry in $zip.Entries) {
        if ($entry.FullName -notmatch $pattern) { continue }
        $target = Join-Path $cache ($entry.FullName -replace '/', '\')
        $parent = Split-Path -Parent $target
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
        $extract = [IO.Compression.ZipFileExtensions]
        $extract::ExtractToFile($entry, $target, $true)
      }
    }
    finally { $zip.Dispose() }
  }
  finally { $memory.Dispose() }

  $hit = Get-SignToolIn -Root $cache -Recurse
  if (-not $hit) {
    throw 'the SDK package carried no signtool.exe, install the Windows SDK'
  }
  Write-Note "fetched $hit"
  return $hit
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

  $stem = Join-Path $BackupDir ($Subject -replace '[^A-Za-z0-9]', '_')
  New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null
  $secure = ConvertTo-SecureString -String $PfxPassword -Force -AsPlainText
  Export-PfxCertificate -Cert $cert -FilePath "$stem.pfx" `
    -Password $secure | Out-Null
  Export-Certificate -Cert $cert -FilePath "$stem.cer" | Out-Null
  Write-Step "backed up the pfx to $stem.pfx"
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
    if (Test-IsAdmin) {
      & certutil -addstore Root $cer | Out-Null
      & certutil -addstore TrustedPeople $cer | Out-Null
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

function Remove-TrustedCertificate {
  param([string]$Subject, [bool]$Apply = $true)

  $stores = 'Cert:\CurrentUser\My', 'Cert:\CurrentUser\Root', `
    'Cert:\LocalMachine\Root', 'Cert:\LocalMachine\TrustedPeople'
  $thumbs = @(Get-ChildItem $stores -ErrorAction SilentlyContinue |
    Where-Object { $_.Subject -eq $Subject } |
    Select-Object -ExpandProperty Thumbprint -Unique)
  if ($thumbs.Count -eq 0) {
    Write-Step "no certificate for $Subject in the stores"
    return
  }

  foreach ($thumbprint in $thumbs) {
    Write-Step "removing certificate $thumbprint"
    $args = @{ Thumbprint = $thumbprint; Apply = $Apply }
    Remove-StoreEntry @args -Store 'My' -UserScope
    Remove-StoreEntry @args -Store 'Root' -UserScope
    if (Test-IsAdmin) {
      Remove-StoreEntry @args -Store 'Root'
      Remove-StoreEntry @args -Store 'TrustedPeople'
    }
  }
}

function Stop-PackageProcesses {
  <#
    Processes running out of the package folder hold its files and stall the
    removal, so they are stopped first. Only executables below the install
    location match, so explorer and dllhost are never touched.
  #>
  param([string]$InstallLocation)

  if (-not $InstallLocation) { return 0 }
  if (-not (Test-Path -LiteralPath $InstallLocation)) { return 0 }
  try {
    $running = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
      $_.Path -and
      $_.Path.StartsWith($InstallLocation,
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
  <#
    Removal takes a minute or more and looks like a hang, so it runs in a
    background job with a heartbeat, and gives up with an explanation instead
    of waiting forever.
  #>
  param([string]$PackageFullName)

  $stopwatch = [Diagnostics.Stopwatch]::StartNew()
  try {
    $job = Start-Job -ErrorAction Stop -ArgumentList $PackageFullName `
      -ScriptBlock {
        param($name) Remove-AppxPackage -Package $name -ErrorAction Stop
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

  $signature = Get-AuthenticodeSignature -FilePath $Path
  if ($NoSign) {
    Write-Step "signing skipped, current status $($signature.Status)"
  }
  elseif ($signature.Status -eq 'Valid') {
    Write-Step "already signed by $($signature.SignerCertificate.Subject)"
    # A release package keeps its signature, and deploying it needs that
    # certificate trusted here.
    Trust-Certificate -Certificate $signature.SignerCertificate `
      -Apply:(-not $WhatIfPreference)
  }
  else {
    Write-Step "not signed ($($signature.Status)), signing it"
    if (-not $WhatIfPreference) {
      $signTool = Find-SignTool
      Write-Note "signtool $signTool"
    }

    $subject = Get-PackagePublisher -Path $Path
    if (-not $subject) { $subject = $Publisher }
    if ($subject -ne $Publisher) {
      Write-Note "the package asks for $subject, signing against that"
    }

    $certificate = Get-OrCreateCertificate -Subject $subject `
      -Apply:(-not $WhatIfPreference)
    if (-not $certificate) { return }
    Trust-Certificate -Certificate $certificate -Apply:(-not $WhatIfPreference)
    if ($WhatIfPreference) {
      Write-Host 'WhatIf: the package would be signed and installed.' `
        -ForegroundColor DarkGray
      return
    }
    $stamp = 'http://timestamp.digicert.com'
    & $signTool sign /fd SHA256 /td SHA256 /tr $stamp `
      /sha1 $certificate.Thumbprint $Path
    if ($LASTEXITCODE -ne 0) {
      throw "signtool failed with exit code $LASTEXITCODE"
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
    if ($PSCmdlet.ShouldProcess($target, 'Remove-AppxPackage')) {
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
  if ($PSCmdlet.ShouldProcess($Path, 'Add-AppxPackage')) {
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
    if (-not $PSCmdlet.ShouldProcess($target, 'Remove-AppxPackage')) {
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
    $apply = -not $WhatIfPreference
    foreach ($name in @(Invoke-ProvisionedPackageCleanup -Name $PackageName `
        -Apply $apply)) {
      Write-Note "provisioned package removed: $name"
    }
  }

  Remove-TrustedCertificate -Subject $Publisher -Apply:(-not $WhatIfPreference)
  if (Test-Path -LiteralPath $BackupDir) {
    Write-Step "deleting $BackupDir"
    if ($PSCmdlet.ShouldProcess($BackupDir, 'Remove-Item')) {
      Remove-Item -LiteralPath $BackupDir -Recurse -Force `
        -ErrorAction SilentlyContinue
    }
  }

  Write-Host ''
  if (Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue) {
    Write-Warn "$PackageName is still installed"
  }
  else {
    Write-Host "OK  $PackageName is gone" -ForegroundColor Green
  }
}

if ($Uninstall) {
  Uninstall-Package
  return
}

$package = Resolve-PackagePath
Write-Step "package : $package"
Install-Package -Path $package

if ($WhatIfPreference) {
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
