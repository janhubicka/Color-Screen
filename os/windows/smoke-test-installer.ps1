# Smoke-test the *installed* Windows package on disposable GitHub CI workers.
# Refuse any target directory that pre-exists, to avoid damaging user files.
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Start-Process -Wait follows descendant processes on Windows. NSIS may
# spawn a helper (including its elevated/uninstaller process), so that wait
# can hang even when the original executable has exited. Wait on the actual
# process handle and always bound the operation.
function Invoke-BoundedProcess {
    param(
        [Parameter(Mandatory = $true)] [string]$Path,
        [Parameter(Mandatory = $true)] [string[]]$Arguments,
        [Parameter(Mandatory = $true)] [string]$Phase,
        [int]$TimeoutSeconds = 120
    )
    Write-Host "Starting $Phase (timeout: $TimeoutSeconds seconds)"
    $process = Start-Process -FilePath $Path -ArgumentList $Arguments -PassThru
    try {
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $processId = $process.Id
            Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
            throw "$Phase timed out after $TimeoutSeconds seconds (PID $processId)"
        }
        $exitCode = $process.ExitCode
        if ($exitCode -ne 0) {
            throw "$Phase failed with exit code $exitCode"
        }
        Write-Host "$Phase completed successfully."
    }
    finally {
        $process.Dispose()
    }
}

$installDir = Join-Path $env:LOCALAPPDATA 'Color-Screen-Installer-Smoke'
if (Test-Path -LiteralPath $installDir) {
    throw "Installer test directory already exists: $installDir"
}

# HKEY_CLASSES_ROOT shows both machine and per-user file associations.
function Get-DefaultClass {
    param([string]$Extension)
    $keyPath = "Registry::HKEY_CLASSES_ROOT\.$Extension"
    if (-not (Test-Path -LiteralPath $keyPath)) {
        return $null
    }
    return (Get-Item -LiteralPath $keyPath).GetValue('', $null)
}

# OpenWithProgids stores a *named* value with empty data, not a default value.
function Assert-OpenWithRegistration {
    param([string]$Extension, [bool]$ShouldExist)
    $keyPath = "Registry::HKEY_CLASSES_ROOT\.$Extension\OpenWithProgids"
    $valueName = "Color-Screen.$Extension"
    $value = $null
    if (Test-Path -LiteralPath $keyPath) {
        $value = (Get-Item -LiteralPath $keyPath).GetValue($valueName, $null)
    }
    if ($ShouldExist -and $null -eq $value) {
        throw "Missing Open With registration $Extension -> $valueName"
    }
    if (-not $ShouldExist -and $null -ne $value) {
        throw "Uninstaller left Open With registration $Extension -> $valueName"
    }
}

# The production installer is now per-user and must not request UAC.
# In CI the user-writable test target is intentionally separate from the
# default LOCALAPPDATA/Programs destination.
$installer = (Resolve-Path -LiteralPath $InstallerPath).Path
$jpgDefaultBefore = Get-DefaultClass 'jpg'
$tiffDefaultBefore = Get-DefaultClass 'tif'
Write-Host "Installing $installer into $installDir"

# /D must be the final NSIS argument. Avoid quoting ambiguities by using an
# user-writable destination (this does not test the interactive wizard).
Invoke-BoundedProcess -Path $installer -Arguments @('/S', "/D=$installDir") -Phase 'NSIS silent installation' -TimeoutSeconds 150

# A system installer may launch an elevated child and return before the last
# file has been written. Poll the expected installed executable, not just the
# bootstrapper's exit status.
$cli = Join-Path $installDir 'colorscreen.exe'
for ($i = 0; $i -lt 60 -and -not (Test-Path -LiteralPath $cli -PathType Leaf); $i++) {
    Start-Sleep -Seconds 1
}

$cli = Join-Path $installDir 'colorscreen.exe'
$gui = Join-Path $installDir 'colorscreen-qt.exe'
$uninstaller = Join-Path $installDir 'uninstall.exe'
foreach ($path in @($cli, $gui, $uninstaller,
                    (Join-Path $installDir 'LICENSE.txt'),
                    (Join-Path $installDir 'README.txt'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Installer omitted required file: $path"
    }
}

# This must run from the copied NSIS payload, not the build/staging tree.
& $cli --version
if ($LASTEXITCODE -ne 0) {
    throw "Installed CLI failed to launch: exit $LASTEXITCODE"
}

# Exercise the Qt GUI from the *installed* directory too. The existing
# portable/staging GUI smoke cannot detect missing files inside the installer.
$fixture = Join-Path $env:GITHUB_WORKSPACE 'testsuite\dufaycolor_nikon_coolsan9000ED_4000DPI_raw.tif'
if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) {
    throw "Missing installed-GUI smoke input: $fixture"
}
Invoke-BoundedProcess -Path $gui -Arguments @(
    '--debug-qt', '--smoke-test', '30000', '--smoke-test-expect-windows', '2',
    $fixture, $fixture
) -Phase 'Installed Qt GUI smoke' -TimeoutSeconds 70

foreach ($ext in @('cspar', 'csprj', 'par', 'jpg', 'tif')) {
    Assert-OpenWithRegistration -Extension $ext -ShouldExist $true
}
foreach ($ext in @('cspar', 'csprj')) {
    $actualClass = Get-DefaultClass $ext
    if ($actualClass -ne "Color-Screen.$ext") {
        throw ".$ext default is $actualClass, expected Color-Screen.$ext"
    }
}
if ((Get-DefaultClass 'jpg') -ne $jpgDefaultBefore -or
    (Get-DefaultClass 'tif') -ne $tiffDefaultBefore) {
    throw 'Installing Color-Screen changed an existing image-app default'
}

Write-Host "Uninstalling the CI-only $installDir installation"
Invoke-BoundedProcess -Path $uninstaller -Arguments @('/S') -Phase 'NSIS silent uninstallation' -TimeoutSeconds 120

# A temporary process may finish deleting NSIS's original uninstaller.
for ($i = 0; $i -lt 30 -and (Test-Path -LiteralPath $installDir); $i++) {
    Start-Sleep -Milliseconds 500
}
if (Test-Path -LiteralPath $installDir) {
    throw "Uninstaller left files in $installDir"
}

foreach ($ext in @('cspar', 'csprj', 'par', 'jpg', 'tif')) {
    Assert-OpenWithRegistration -Extension $ext -ShouldExist $false
}
foreach ($ext in @('cspar', 'csprj')) {
    if ((Get-DefaultClass $ext) -eq "Color-Screen.$ext") {
        throw "Uninstaller left Color-Screen as the default for .$ext"
    }
}
if ((Get-DefaultClass 'jpg') -ne $jpgDefaultBefore -or
    (Get-DefaultClass 'tif') -ne $tiffDefaultBefore) {
    throw 'Uninstalling Color-Screen changed an existing image-app default'
}
Write-Host 'NSIS silent install, CLI launch, registry and uninstall smoke passed.'
