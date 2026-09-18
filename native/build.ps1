$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++.' }
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$install) { throw 'MSVC x64 tools were not found.' }
$version = (Get-Content (Join-Path $install 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt')).Trim()
$toolset = Join-Path $install "VC/Tools/MSVC/$version"
$sdk = "${env:ProgramFiles(x86)}/Windows Kits/10"
$sdkVersion = (Get-ChildItem "$sdk/Include" -Directory | Where-Object { Test-Path "$($_.FullName)/um/windows.h" } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
if (!$sdkVersion) { throw 'Windows SDK was not found.' }
$env:PATH = "$toolset/bin/Hostx64/x64;$sdk/bin/$sdkVersion/x64;$env:PATH"
$env:INCLUDE = "$toolset/include;$sdk/Include/$sdkVersion/ucrt;$sdk/Include/$sdkVersion/shared;$sdk/Include/$sdkVersion/um;$sdk/Include/$sdkVersion/winrt"
$env:LIB = "$toolset/lib/x64;$sdk/Lib/$sdkVersion/ucrt/x64;$sdk/Lib/$sdkVersion/um/x64"
Push-Location $PSScriptRoot
try {
    [void](New-Item -ItemType Directory -Path build,dist,output -Force)
    & rc.exe /nologo /fo build/pico.res resources.rc
    if ($LASTEXITCODE -ne 0) { throw "Resource compilation failed: $LASTEXITCODE" }
    # Keep the production translation units and link dependencies in one place.
    $sources = @(
        'src/main.cpp', 'src/embedded_console.cpp', 'src/system_core.cpp',
        'src/system_hardware.cpp', 'src/system_network.cpp', 'src/system_network_trace.cpp',
        'src/system_diagnostics.cpp', 'src/system_security.cpp', 'src/system_console.cpp',
        'src/system_index.cpp', 'src/system_window.cpp'
    )
    $options = @('/nologo','/std:c++20','/utf-8','/O2','/MT','/EHsc','/W4','/WX',
        '/DUNICODE','/D_UNICODE','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX','/D_WIN32_WINNT=0x0A00',
        '/Fo:build/','/Fe:dist/PicoPet.exe')
    $libraries = @('user32.lib','gdi32.lib','shell32.lib','ole32.lib','windowscodecs.lib',
        'wtsapi32.lib','dwmapi.lib','advapi32.lib','powrprof.lib','psapi.lib','winmm.lib',
        'comctl32.lib','comdlg32.lib','iphlpapi.lib','ws2_32.lib','winsqlite3.lib','setupapi.lib',
        'cfgmgr32.lib','dnsapi.lib','uxtheme.lib','wevtapi.lib','wscapi.lib','tbs.lib')
    & cl.exe @options @sources build/pico.res /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /DYNAMICBASE /NXCOMPAT @libraries
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
} finally { Pop-Location }
Write-Host "Built: $PSScriptRoot/dist/PicoPet.exe"
