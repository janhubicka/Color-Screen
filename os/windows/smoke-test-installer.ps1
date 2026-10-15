# Smoke-test the *installed* Windows package on disposable GitHub CI workers.
# Refuse any target directory that pre-exists, to avoid damaging user files.
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$installDir = Join-Path $env:SystemDrive 'Color-Screen-Installer-Smoke'
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

$installer = (Resolve-Path -LiteralPath $InstallerPath).Path
$jpgDefaultBefore = Get-DefaultClass 'jpg'
$tiffDefaultBefore = Get-DefaultClass 'tif'
Write-Host "Installing $installer into $installDir"

# /D must be the final NSIS argument. Avoid quoting ambiguities by using an
# isolated destination without spaces (this does not test interactive /D).
$installation = Start-Process -FilePath $installer -ArgumentList @('/S', "/D=$installDir") -Wait -PassThru
if ($installation.ExitCode -ne 0) {
    throw "Silent installer exited with $($installation.ExitCode)"
}

$cli = Join-Path $installDir 'bin\colorscreen.exe'
$gui = Join-Path $installDir 'bin\colorscreen-qt.exe'
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
$guiRun = Start-Process -FilePath $gui -ArgumentList @(
    '--debug-qt', '--smoke-test', '30000', '--smoke-test-expect-windows', '2',
    $fixture, $fixture
) -Wait -PassThru
if ($guiRun.ExitCode -ne 0) {
    throw "Installed GUI smoke failed: exit $($guiRun.ExitCode)"
}

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
$removal = Start-Process -FilePath $uninstaller -ArgumentList '/S' -Wait -PassThru
if ($removal.ExitCode -ne 0) {
    throw "Silent uninstaller exited with $($removal.ExitCode)"
}

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
