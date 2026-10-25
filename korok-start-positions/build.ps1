param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw '.NET Framework 4.x compiler required' }
if (Test-Path -LiteralPath $Output) { throw 'Output already exists' }
$parent=Split-Path -Parent ([IO.Path]::GetFullPath($Output))
[IO.Directory]::CreateDirectory($parent) | Out-Null
& $compiler /nologo /target:winexe /platform:x64 /optimize+ ('/out:'+[IO.Path]::GetFullPath($Output)) /reference:System.Windows.Forms.dll /reference:System.Drawing.dll ('/resource:'+(Join-Path $PSScriptRoot 'starts.patch')+',starts.patch') (Join-Path $PSScriptRoot 'Setup.cs')
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
