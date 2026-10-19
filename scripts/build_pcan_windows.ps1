param(
    [string]$QtDir = $env:QTDIR,
    [string]$VcVars64,
    [string]$ClangCl,
    [string]$BuildDir,
    [string]$PcanBasicDll
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$QtDir) { $QtDir = Join-Path $repo 'build\qt\5.15.2\msvc2019_64' }
$QtDir = (Resolve-Path -LiteralPath $QtDir).Path
if (!$BuildDir) { $BuildDir = Join-Path $repo 'build\pcan' }
if (!$VcVars64) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'Install the Visual Studio Desktop development with C++ workload.' }
    $VcVars64 = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
}
$VcVars64 = (Resolve-Path -LiteralPath $VcVars64).Path
if (!$ClangCl) {
    $localClang = Join-Path $repo 'build\llvm\bin\clang-cl.exe'
    if (Test-Path -LiteralPath $localClang) {
        $ClangCl = $localClang
    } else {
        $clangCommand = Get-Command clang-cl.exe -ErrorAction SilentlyContinue
        if (!$clangCommand) { throw 'Install LLVM and put clang-cl.exe on PATH, or pass -ClangCl.' }
        $ClangCl = $clangCommand.Source
    }
}
$ClangCl = (Resolve-Path -LiteralPath $ClangCl).Path

# Import the compiler environment into this PowerShell process.
cmd /d /c "`"$VcVars64`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
if ($LASTEXITCODE) { throw 'Could not initialize the MSVC environment.' }
$env:PATH = (Join-Path $QtDir 'bin') + ';' + (Split-Path -Parent $ClangCl) + ';' + $env:PATH
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
Push-Location $BuildDir
try {
    # The bundled ESP flasher C sources use GNU packed/weak attributes and C99
    # initializers. clang-cl handles them while producing MSVC-compatible objects.
    & (Join-Path $QtDir 'bin\qmake.exe') (Join-Path $repo 'vesc_tool.pro') -config release 'CONFIG+=release_win build_original exclude_fw' 'QMAKE_CXXFLAGS+=/MP4' 'QMAKE_CC=clang-cl.exe'
    if ($LASTEXITCODE) { throw 'qmake failed.' }
    & nmake /nologo
    if ($LASTEXITCODE) { throw 'Build failed.' }
    $exe = Get-ChildItem -LiteralPath 'build\win' -Filter 'vesc_tool_*.exe' | Select-Object -First 1
    if (!$exe) { throw 'Build produced no executable.' }
    $runtime = Join-Path $BuildDir 'windows'
    New-Item -ItemType Directory -Force -Path $runtime | Out-Null
    Copy-Item -LiteralPath $exe.FullName -Destination $runtime
    $runtimeExe = Join-Path $runtime $exe.Name
    & (Join-Path $QtDir 'bin\windeployqt.exe') --release --no-translations --qmldir (Join-Path $repo 'mobile') $runtimeExe
    if ($LASTEXITCODE) { throw 'Qt runtime deployment failed.' }
    # CAN plugins are not reliably included by windeployqt dependency scanning.
    New-Item -ItemType Directory -Force -Path (Join-Path $runtime 'canbus') | Out-Null
    Copy-Item -LiteralPath (Join-Path $QtDir 'plugins\canbus\qtpeakcanbus.dll') -Destination (Join-Path $runtime 'canbus')
    if ($PcanBasicDll) {
        Copy-Item -LiteralPath $PcanBasicDll -Destination (Join-Path $runtime 'PCANBasic.dll')
    }
    Write-Host "Ready: $runtimeExe"
} finally {
    Pop-Location
}
