# Build static tt-unlock.exe (Windows x64, MinGW / MSYS2)
# Requires g++ and gcc on PATH (e.g. MSYS2 mingw64).
$ErrorActionPreference = "Stop"

function Find-Tool([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @(
        "C:\msys64\mingw64\bin\$name.exe",
        "C:\msys2\mingw64\bin\$name.exe",
        "D:\msys64\mingw64\bin\$name.exe",
        "D:\msys2\mingw64\bin\$name.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

$gpp = Find-Tool "g++"
$gcc = Find-Tool "gcc"
if (-not $gpp -or -not $gcc) {
    throw "g++/gcc not found. Install MSYS2 MinGW64 and add mingw64\bin to PATH."
}

$root = $PSScriptRoot
Set-Location $root
New-Item -ItemType Directory -Force -Path "$root\build", "$root\release" | Out-Null

Write-Host "g++ => $gpp"
& $gpp --version | Select-Object -First 1

$obj = "$root\build\miniz.o"
Write-Host "Compiling miniz.c ..."
& $gcc -O2 -DNDEBUG -I"$root\third_party\miniz" -c "$root\third_party\miniz\miniz.c" -o $obj
if ($LASTEXITCODE -ne 0) { throw "miniz compile failed" }

$out = "$root\build\tt-unlock.exe"
$cxxflags = @(
    "-std=c++17", "-O2", "-DNDEBUG", "-D_WIN32_WINNT=0x0A00",
    "-Isrc", "-Ithird_party/miniz",
    "-Wall", "-Wextra", "-Wno-unused-parameter",
    "-ffunction-sections", "-fdata-sections"
)
$ldflags = @(
    "-static", "-static-libgcc", "-static-libstdc++",
    "-Wl,--gc-sections", "-s",
    "-ladvapi32", "-lshell32", "-luser32", "-lkernel32"
)

Write-Host "Linking $out ..."
& $gpp @cxxflags -o $out "$root\src\main.cpp" $obj @ldflags
if ($LASTEXITCODE -ne 0) { throw "link failed" }

Copy-Item $out "$root\release\tt-unlock.exe" -Force
$size = (Get-Item $out).Length
Write-Host ""
Write-Host "OK  $out  ($([math]::Round($size/1KB,1)) KB static)"
Write-Host "    release\tt-unlock.exe"
