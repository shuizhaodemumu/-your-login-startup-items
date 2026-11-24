$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$bin=Join-Path $project 'bin'
if(-not (Test-Path -LiteralPath "$bin\DragonGuardFixture.exe")){throw 'Run tests/run.ps1 first to build the fixture.'}
$source=@('ui\App.cs','ui\Model.cs','ui\Native.cs','tests\UiTests.cs') | ForEach-Object {Join-Path $project $_}
& $csc /nologo /target:exe /main:UiTests /platform:x64 /codepage:65001 "/out:$bin\UiTests.exe" /reference:System.dll /reference:System.Core.dll /reference:System.Drawing.dll /reference:System.Windows.Forms.dll /reference:System.Runtime.Serialization.dll "/resource:$project\assets\welcome.png,welcome.png" "/resource:$project\assets\guard.png,guard.png" "/resource:$project\assets\recover.png,recover.png" $source
if($LASTEXITCODE -ne 0){throw 'UI test fixture build failed'}
& "$bin\UiTests.exe"
if($LASTEXITCODE -ne 0){throw 'UI behavior test failed'}
