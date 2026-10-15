# Smoke-test the *installed* Windows package on disposable GitHub CI workers.
# Refuse any target directory that pre-exists, to avoid damaging user files.
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Launch via CreateProcess instead of PowerShell's ShellExecute path:
# Windows may show SmartScreen/UAC shell UI without an interactive CI desktop.
# Pass exact arguments (including /D=<path>) and wait on the actual process
# handle, bounded in time. Report regularly if package extraction stalls.
function Invoke-BoundedProcess {
    param(
        [Parameter(Mandatory = $true)] [string]$Path,
        [Parameter(Mandatory = $true)] [string[]]$Arguments,
        [Parameter(Mandatory = $true)] [string]$Phase,
        [int]$TimeoutSeconds = 120
    )
    Write-Host "Starting $Phase (timeout: $TimeoutSeconds seconds)"
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Path
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WorkingDirectory = Split-Path -Parent $Path
    foreach ($argument in $Arguments) {
        [void]$info.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) {
            throw "$Phase could not start"
        }
        Write-Host "$Phase started as PID $($process.Id), args: $($Arguments -join ' ')"
        $watch = [System.Diagnostics.Stopwatch]::StartNew()
        $lastReport = 0
        while (-not $process.WaitForExit(2000)) {
            if ($watch.Elapsed.TotalSeconds - $lastReport -ge 15) {
                $lastReport = $watch.Elapsed.TotalSeconds
                $process.Refresh()
                $files = 0
                if (Test-Path -LiteralPath $installDir) {
                    $files = @(Get-ChildItem -LiteralPath $installDir -File -Recurse -ErrorAction SilentlyContinue).Count
                }
                Write-Host ("$Phase pending: {0:n0}s elapsed, PID {1}, CPU {2:n1}s, {3} extracted files, window '{4}'" -f
                    $watch.Elapsed.TotalSeconds, $process.Id, $process.TotalProcessorTime.TotalSeconds,
                    $files, $process.MainWindowTitle)
            }
            if ($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                $processId = $process.Id
                Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
                throw "$Phase timed out after $TimeoutSeconds seconds (PID $processId)"
            }
        }
        if ($process.ExitCode -ne 0) {
            throw "$Phase failed with exit code $($process.ExitCode)"
        }
        Write-Host "$Phase completed successfully after $([int]$watch.Elapsed.TotalSeconds)s."
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
$legacyParamsBefore = Get-DefaultClass 'par'
$jsonParamsBefore = Get-DefaultClass 'cspar'
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

# Verify Add/Remove Programs registration belongs to this user and the
# quoted command survives a user profile containing whitespace.
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen'
if (-not (Test-Path -LiteralPath $uninstallKey)) {
    throw "Installed package did not register a per-user uninstaller"
}
$uninstallRecord = Get-ItemProperty -LiteralPath $uninstallKey
if ($uninstallRecord.InstallLocation -ne $installDir -or
    $uninstallRecord.UninstallString -ne ('"{0}"' -f $uninstaller) -or
    $uninstallRecord.QuietUninstallString -ne ('"{0}" /S' -f $uninstaller)) {
    throw "Installed package exposed invalid Add/Remove Programs metadata"
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

# The executable supports image and stitch-project Explorer opens. Parameter
# sidecars require an already chosen scan and are loaded from inside the GUI;
# they must not acquire a broken Explorer "Open" command.
foreach ($ext in @('csprj', 'jpg', 'tif')) {
    Assert-OpenWithRegistration -Extension $ext -ShouldExist $true
}
foreach ($ext in @('par', 'cspar')) {
    Assert-OpenWithRegistration -Extension $ext -ShouldExist $false
}
foreach ($ext in @('csprj')) {
    $actualClass = Get-DefaultClass $ext
    if ($actualClass -ne "Color-Screen.$ext") {
        throw ".$ext default is $actualClass, expected Color-Screen.$ext"
    }
}
if ((Get-DefaultClass 'jpg') -ne $jpgDefaultBefore -or
    (Get-DefaultClass 'tif') -ne $tiffDefaultBefore -or
    (Get-DefaultClass 'par') -ne $legacyParamsBefore -or
    (Get-DefaultClass 'cspar') -ne $jsonParamsBefore) {
    throw 'Installing Color-Screen changed an existing image/parameter default'
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

if (Test-Path -LiteralPath $uninstallKey) {
    throw "Silent uninstall left a per-user Add/Remove Programs entry"
}

foreach ($ext in @('cspar', 'csprj', 'par', 'jpg', 'tif')) {
    Assert-OpenWithRegistration -Extension $ext -ShouldExist $false
}
foreach ($ext in @('csprj')) {
    if ((Get-DefaultClass $ext) -eq "Color-Screen.$ext") {
        throw "Uninstaller left Color-Screen as the default for .$ext"
    }
}
if ((Get-DefaultClass 'jpg') -ne $jpgDefaultBefore -or
    (Get-DefaultClass 'tif') -ne $tiffDefaultBefore -or
    (Get-DefaultClass 'par') -ne $legacyParamsBefore -or
    (Get-DefaultClass 'cspar') -ne $jsonParamsBefore) {
    throw 'Uninstalling Color-Screen changed an existing image/parameter default'
}
Write-Host 'NSIS silent install, CLI launch, registry and uninstall smoke passed.'
