<#
.SYNOPSIS
  KryptonUI developer helper (Windows). Enters the Visual Studio developer
  environment itself, so it works from any PowerShell.

.EXAMPLE
  pwsh tools/dev.ps1 configure
  pwsh tools/dev.ps1 build -Config Release
  pwsh tools/dev.ps1 test
  pwsh tools/dev.ps1 preview --dark --tabs 200
  pwsh tools/dev.ps1 screenshot out.png --dark
  pwsh tools/dev.ps1 build -Preset headless
.PARAMETER Jobs
  Optional build parallelism. Omit for automatic jobs; 0 requests unlimited
  native Ninja jobs. Positive values select a compilation job count.
#>
[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [ValidateSet('configure', 'build', 'test', 'preview', 'screenshot', 'clean')]
  [string]$Command = 'build',

  [ValidateSet('Debug', 'Release')]
  [string]$Config = 'Debug',

  [ValidateSet('windows', 'windows-clang', 'headless')]
  [string]$Preset = 'windows',

  [ValidateRange(0, 2147483647)]
  [Nullable[int]]$Jobs = $null,

  [Parameter(ValueFromRemainingArguments)]
  [string[]]$Rest = @()
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "out/build/$Preset"
$binDir = Join-Path $buildDir "bin/$Config"
# The headless preset is single-purpose; the Windows ones are per config.
$buildPreset = if ($Preset -eq 'headless') { 'headless' } else { "$Preset-$($Config.ToLower())" }

function Enter-DevShell {
  if ($env:VSCMD_VER) { return }  # already inside a developer shell
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { throw 'Visual Studio was not found (no vswhere.exe).' }
  $vs = & $vswhere -latest -prerelease -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) { throw 'No Visual Studio installation with the C++ x64 tools was found.' }
  # VS 2026's developer scripts also invoke vswhere by name.
  $installerDir = Split-Path -Parent $vswhere
  if ($installerDir -notin ($env:PATH -split ';')) { $env:PATH = "$installerDir;$env:PATH" }
  Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
  Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
  & $Exe @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$Exe $($Arguments -join ' ') failed ($LASTEXITCODE)" }
}

function Invoke-Build {
  if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt')) -or
      -not (Test-Path -LiteralPath (Join-Path $buildDir "build-$Config.ninja"))) {
    Invoke-Checked cmake @('--preset', $Preset)
  }
  $buildArguments = @('--build', '--preset', $buildPreset)
  if ($null -ne $Jobs) {
    if ($Jobs -eq 0) {
      $buildArguments += @('--', '-j0')
    } else {
      $buildArguments += @('--parallel', [string]$Jobs)
    }
  }
  Invoke-Checked cmake $buildArguments
}

Push-Location $root
try {
  Enter-DevShell
  switch ($Command) {
    'configure' { Invoke-Checked cmake @('--preset', $Preset) }
    'build' { Invoke-Build }
    'test' {
      Invoke-Build
      Invoke-Checked ctest @('--preset', $buildPreset)
    }
    'preview' {
      Invoke-Build
      $launch = @{ FilePath = (Join-Path $binDir 'kr_preview.exe'); WindowStyle = 'Hidden' }
      if ($Rest.Count -gt 0) { $launch.ArgumentList = $Rest }
      Start-Process @launch
    }
    'screenshot' {
      if ($Rest.Count -lt 1) { throw 'usage: dev.ps1 screenshot <file.png> [preview options]' }
      Invoke-Build
      $target = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Rest[0]))
      $arguments = @('--screenshot', "`"$target`"") + ($Rest | Select-Object -Skip 1)
      # The window must be shown: a hidden window has no composed content to
      # capture.
      $process = Start-Process -FilePath (Join-Path $binDir 'kr_preview.exe') `
        -ArgumentList $arguments -PassThru -Wait
      if ($process.ExitCode -ne 0 -or -not (Test-Path $target)) {
        throw "screenshot failed (exit code $($process.ExitCode))"
      }
      Write-Host "Saved $target"
    }
    'clean' {
      if (Test-Path -LiteralPath $buildDir) {
        $resolvedBuild = (Resolve-Path -LiteralPath $buildDir).Path
        $expectedBuild = [System.IO.Path]::GetFullPath((Join-Path $root "out/build/$Preset"))
        if ($resolvedBuild -ne $expectedBuild) { throw "Unexpected build directory: $resolvedBuild" }
        Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
      }
    }
  }
}
finally {
  Pop-Location
}
