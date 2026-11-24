param(
    [string]$Cargo = 'cargo',
    [string]$Target = 'x86_64-pc-windows-msvc',
    [switch]$Test
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if ($Target -notin @('x86_64-pc-windows-msvc', 'x86_64-pc-windows-gnu')) { throw 'Only Windows x64 targets are supported.' }
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc)) { throw '.NET Framework 4.x C# compiler is required.' }
Push-Location $projectRoot
try {
    if ($Test) {
        & $Cargo test --locked --offline --target $Target
        if ($LASTEXITCODE -ne 0) { throw 'Rust tests failed.' }
    }
    & $Cargo build --release --locked --offline --target $Target
    if ($LASTEXITCODE -ne 0) { throw 'Rust build failed.' }
    $bin = Join-Path $projectRoot 'bin'
    New-Item -ItemType Directory -Force -Path $bin | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot "target\$Target\release\dragon_guard_core.dll") -Destination $bin -Force
    $sources = @('ui\App.cs', 'ui\Model.cs', 'ui\Native.cs') | ForEach-Object { Join-Path $projectRoot $_ }
    & $csc /nologo /target:winexe /platform:x64 /optimize+ /codepage:65001 "/out:$bin\LinglongGuard.exe" "/win32icon:$projectRoot\assets\dragon.ico" "/win32manifest:$projectRoot\ui\app.manifest" /reference:System.dll /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll /reference:System.Runtime.Serialization.dll "/resource:$projectRoot\assets\welcome.png,welcome.png" "/resource:$projectRoot\assets\guard.png,guard.png" "/resource:$projectRoot\assets\recover.png,recover.png" $sources
    if ($LASTEXITCODE -ne 0) { throw 'Windows UI build failed.' }
    Write-Output "Built: $bin\LinglongGuard.exe"
} finally { Pop-Location }
