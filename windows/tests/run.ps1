param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if(-not $SkipBuild){& (Join-Path $project 'build.ps1') -Test}
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$bin=Join-Path $project 'bin'
& $csc /nologo /target:winexe /platform:x64 /codepage:65001 "/out:$bin\DragonGuardFixture.exe" /reference:System.dll /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll (Join-Path $PSScriptRoot 'Fixture.cs')
if($LASTEXITCODE -ne 0){throw 'Fixture compilation failed'}
& $csc /nologo /target:exe /platform:x64 /codepage:65001 "/out:$bin\Integration.exe" /reference:System.dll /reference:System.Core.dll /reference:System.Windows.Forms.dll /reference:System.Runtime.Serialization.dll (Join-Path $PSScriptRoot 'Integration.cs') (Join-Path $project 'ui\Native.cs') (Join-Path $project 'ui\Model.cs')
if($LASTEXITCODE -ne 0){throw 'Test compilation failed'}
& (Join-Path $bin 'Integration.exe')
if($LASTEXITCODE -ne 0){throw 'Native integration tests failed'}
