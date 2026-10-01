param(
    [string]$OriginalRaylib = ''
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dist = Join-Path $root 'dist'
$build = Join-Path $root 'build'
$stage = Join-Path $dist ('.stage-' + [guid]::NewGuid().ToString('N'))
$modFolder = Join-Path $stage 'nulm-background'
$dll = Join-Path $modFolder 'nulm_background.dll'
$manifestSource = Join-Path $root 'mod\modconfig.json'
$zip = Join-Path $dist 'TaikoNauts-NULM-Background-v1.0.0.zip'
$gcc = (Get-Command gcc.exe -ErrorAction Stop).Source
$objdump = (Get-Command objdump.exe -ErrorAction Stop).Source

New-Item -ItemType Directory -Path $dist,$build,$modFolder -Force | Out-Null

try {
    $manifest = [IO.File]::ReadAllText($manifestSource) | ConvertFrom-Json
    if ([string]$manifest.id -ne 'nulm-background' -or
        [string]$manifest.version -ne '1.0.0' -or
        [string]$manifest.dll -ne 'nulm_background.dll' -or
        $manifest.enabled -isnot [bool]) {
        throw 'modconfig.json does not match the NULM Background v1.0.0 package.'
    }

    & $gcc -shared -O2 -s -Wall -Wextra -Werror `
        -I (Join-Path $root 'include') `
        (Join-Path $root 'src\nulm_background.c') `
        (Join-Path $root 'src\nulm.c') `
        (Join-Path $root 'src\game_clear.c') `
        -o $dll -static-libgcc -luser32
    if ($LASTEXITCODE -ne 0) { throw "Mod build failed with exit code $LASTEXITCODE" }

    $dump = & $objdump -p $dll
    if ($LASTEXITCODE -ne 0 -or -not ($dump -match 'TaikoNautsModInit')) {
        throw 'The built DLL does not export TaikoNautsModInit.'
    }

    $exportSmoke = Join-Path $build 'export_smoke.exe'
    & $gcc -O2 -s -o $exportSmoke (Join-Path $root 'tests\export_smoke.c')
    if ($LASTEXITCODE -ne 0) { throw 'Export smoke build failed.' }
    & $exportSmoke $dll
    if ($LASTEXITCODE -ne 0) { throw 'Export smoke failed.' }

    $inspect = Join-Path $build 'inspect.exe'
    & $gcc -O2 -Wall -Wextra -Werror -o $inspect `
        (Join-Path $root 'tests\inspect.c') (Join-Path $root 'src\nulm.c')
    if ($LASTEXITCODE -ne 0) { throw 'Inspect tool build failed.' }

    # The rendering smoke needs the game's raylib DLL and a game folder that
    # already holds mods\nulm-background with a config.json and packs.
    if ($OriginalRaylib -ne '') {
        $gameDir = Split-Path -Parent $OriginalRaylib
        $smoke = Join-Path $build 'render_smoke.exe'
        & $gcc -O2 -Wall -Wextra -Werror -I (Join-Path $root 'include') `
            -o $smoke (Join-Path $root 'tests\render_smoke.c')
        if ($LASTEXITCODE -ne 0) { throw 'Render smoke build failed.' }
        $png = Join-Path $build 'render_smoke.png'
        & $smoke $OriginalRaylib $gameDir $png 120 $dll
        if ($LASTEXITCODE -ne 0) { throw 'Render smoke failed.' }
        Write-Host "Rendered $png" -ForegroundColor Cyan
    }

    Copy-Item -LiteralPath $manifestSource -Destination (Join-Path $modFolder 'modconfig.json')
    Copy-Item -LiteralPath (Join-Path $root 'mod\config.example.json') `
        -Destination (Join-Path $modFolder 'config.example.json')

    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -LiteralPath $modFolder -DestinationPath $zip -CompressionLevel Optimal

    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    Write-Host "Built $zip" -ForegroundColor Green
    Write-Host "SHA256 $hash" -ForegroundColor Cyan
} finally {
    $resolvedDist = [IO.Path]::GetFullPath($dist)
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    if ($resolvedStage.StartsWith($resolvedDist + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedStage).StartsWith('.stage-', [StringComparison]::Ordinal) -and
        (Test-Path -LiteralPath $resolvedStage)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
