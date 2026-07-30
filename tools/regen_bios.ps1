[CmdletBinding()]
param(
    [string]$BiosRom = $env:PSXRECOMP_BIOS_ROM,
    [string]$BuildDir = $env:PSXRECOMP_BIOS_BUILD,
    [string]$OutputDir = $env:PSXRECOMP_BIOS_OUT,
    [string]$SeedsPath = $env:PSXRECOMP_BIOS_SEEDS
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Get-RootedPath([string]$Path, [string]$Default) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        $Path = $Default
    }
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $Root $Path))
}

$BiosRom = Get-RootedPath $BiosRom "bios\SCPH1001.BIN"
$OutputDir = Get-RootedPath $OutputDir "generated"
$SeedsPath = Get-RootedPath $SeedsPath "recompiler\seeds\phase2_ghidra_seeds.json"

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildCandidates = @(
        (Join-Path $Root "recompiler\build-t2"),
        (Join-Path $Root "recompiler\build")
    )
    $BuildCandidates += Get-ChildItem -Path (Join-Path $Root "recompiler") `
        -Directory -Filter "cmake-build*" -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName

    $BuildDir = $BuildCandidates |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ "CMakeCache.txt") } |
        Select-Object -First 1

    if (-not $BuildDir) {
        throw "Nenhum build configurado do recompiler foi encontrado. Execute primeiro a etapa 1 de PlusAlphaProject\BUILD_LOCAL.md."
    }
}
else {
    $BuildDir = Get-RootedPath $BuildDir "recompiler\build"
}

$CMakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $CMakeCommand) {
    $CMakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
}
if (-not $CMakeCommand) {
    $FallbackCMake = Join-Path $env:ProgramFiles "CMake\bin\cmake.exe"
    if (Test-Path -LiteralPath $FallbackCMake -PathType Leaf) {
        $CMakePath = $FallbackCMake
    }
    else {
        throw "CMake não encontrado no PowerShell. Adicione-o ao PATH ou informe uma instalação válida."
    }
}
else {
    $CMakePath = $CMakeCommand.Source
}

foreach ($Required in @($BiosRom, $SeedsPath, (Join-Path $BuildDir "CMakeCache.txt"))) {
    if (-not (Test-Path -LiteralPath $Required -PathType Leaf)) {
        throw "Entrada obrigatória ausente: $Required"
    }
}

Write-Host "regen_bios: compilando psxrecomp-bios em $BuildDir"
& $CMakePath --build $BuildDir --target psxrecomp-bios
if ($LASTEXITCODE -ne 0) {
    throw "A compilação de psxrecomp-bios falhou com código $LASTEXITCODE"
}

$Emitter = Join-Path $BuildDir "psxrecomp-bios.exe"
if (-not (Test-Path -LiteralPath $Emitter -PathType Leaf)) {
    $Emitter = Join-Path $BuildDir "psxrecomp-bios"
}
if (-not (Test-Path -LiteralPath $Emitter -PathType Leaf)) {
    throw "psxrecomp-bios não foi encontrado após a compilação em: $BuildDir"
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "regen_bios: gerando $BiosRom -> $OutputDir"
& $Emitter $BiosRom $OutputDir --emit-full $SeedsPath
if ($LASTEXITCODE -ne 0) {
    throw "A geração da BIOS falhou com código $LASTEXITCODE"
}

$ExpectedOutputs = @(
    (Join-Path $OutputDir "SCPH1001_full.c"),
    (Join-Path $OutputDir "SCPH1001_dispatch.c")
)
foreach ($ExpectedOutput in $ExpectedOutputs) {
    if (-not (Test-Path -LiteralPath $ExpectedOutput -PathType Leaf)) {
        throw "O emissor terminou sem produzir: $ExpectedOutput"
    }
}

# Deve permanecer sincronizado com tools/bios_emitter_fingerprint.sh e com a
# verificação de consistência de runtime/runtime.cmake.
[string[]]$FingerprintInputs = @(
    "recompiler/src/full_function_emitter.cpp",
    "recompiler/src/full_function_emitter.h",
    "recompiler/src/strict_translator.cpp",
    "recompiler/src/main_bios.cpp",
    "recompiler/src/function_discovery.cpp",
    "recompiler/src/control_flow.cpp",
    "recompiler/src/function_analysis.cpp",
    "recompiler/src/mips_decoder.cpp",
    "recompiler/src/bios_slice_walker.cpp",
    "recompiler/src/basic_block.cpp",
    "runtime/include/psx_cyc.h",
    "runtime/include/psx_instr_cost.h",
    "recompiler/seeds/phase2_ghidra_seeds.json"
) | Where-Object { Test-Path -LiteralPath (Join-Path $Root $_) }

[Array]::Sort($FingerprintInputs, [StringComparer]::Ordinal)
$FingerprintLines = foreach ($RelativePath in $FingerprintInputs) {
    $FileHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath (Join-Path $Root $RelativePath)).Hash.ToLowerInvariant()
    # Equivale à saída de `sha256sum --binary`: hash, espaço, `*` e caminho.
    "$FileHash *$RelativePath`n"
}

$FingerprintBytes = [Text.Encoding]::UTF8.GetBytes(($FingerprintLines -join ""))
$Sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $CombinedHashBytes = $Sha256.ComputeHash($FingerprintBytes)
}
finally {
    $Sha256.Dispose()
}
$CombinedHash = ([BitConverter]::ToString($CombinedHashBytes) -replace "-", "").ToLowerInvariant()
$FingerprintPath = Join-Path $OutputDir "SCPH1001.emitter.sha"
$Utf8WithoutBom = New-Object Text.UTF8Encoding($false)
[IO.File]::WriteAllText($FingerprintPath, "$CombinedHash`n", $Utf8WithoutBom)

Write-Host "regen_bios: BIOS gerada e impressão digital gravada em $FingerprintPath"
