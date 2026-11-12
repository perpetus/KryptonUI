[CmdletBinding()]
param([Parameter(Mandatory)][string]$ArtifactDirectory)

# Production-only CI build. The developer's out/build/windows graph is separate.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$buildDirectory = Join-Path $root 'out/build/windows-ci'
$ninja = (Get-Command ninja.exe -ErrorAction Stop).Source
$ninjaVersion = & $ninja --version
if ($LASTEXITCODE -ne 0 -or $ninjaVersion -ne '1.13.2') {
  throw 'Use the verified Ninja 1.13.2 before opening the CI graph.'
}
if (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt')) {
  foreach ($name in @('.ninja_log', '.ninja_deps')) {
    if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory $name))) {
      throw "Existing CI graph has missing $name; preserve it for inspection."
    }
  }
  if ((Get-Content -LiteralPath (Join-Path $buildDirectory '.ninja_log') -TotalCount 1) -ne '# ninja log v7') {
    throw 'Existing CI Ninja log is incompatible; no graph was opened.'
  }
  $makeProgram = Get-Content -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') |
    Where-Object { $_ -like 'CMAKE_MAKE_PROGRAM:FILEPATH=*' }
  if (-not $makeProgram -or $makeProgram.Substring($makeProgram.IndexOf('=') + 1).Replace('/', '\') -ne $ninja.Replace('/', '\')) {
    throw 'CI graph uses a different Ninja executable; preserve the graph.'
  }
}

$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer'
$visualStudio = & (Join-Path $installer 'vswhere.exe') -latest -prerelease -products '*' `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio x64 C++ tools are required.' }
$env:PATH = "$installer;$env:PATH"
Import-Module (Join-Path $visualStudio 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation `
  -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$logs = Join-Path $root 'out/ci'
New-Item -ItemType Directory -Path $logs -Force | Out-Null

function Invoke-CMake([string]$LogName, [string[]]$Arguments) {
  & cmake @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $logs $LogName)
  if ($LASTEXITCODE -ne 0) { throw "CMake failed; see out/ci/$LogName." }
}

Push-Location -LiteralPath $root
try {
  Invoke-CMake 'configure.log' @('--preset', 'windows', '-B', $buildDirectory,
    "-DCMAKE_MAKE_PROGRAM:FILEPATH=$ninja", '-DKR_BUILD_TESTS=OFF', '-DKR_BUILD_PREVIEW=OFF')
  Invoke-CMake 'build.log' @('--build', $buildDirectory, '--config', 'Release',
    '--target', 'kr_shell', 'kr_abi_c_check', '--parallel', '2')
  Invoke-CMake 'install.log' @('--install', $buildDirectory, '--config', 'Release',
    '--component', 'Runtime', '--prefix', $ArtifactDirectory)

  $files = @('krypton_ui.dll', 'Microsoft.WindowsAppRuntime.Bootstrap.dll') +
    (@('hr_classic.png', 'hr_compact.png', 'hr_dyn.png', 'vt_compact.png',
       'vt_sidebar.png', 'vt_sidebar_title.png') | ForEach-Object { "assets/settings/$_" })
  $hashes = [ordered]@{}
  foreach ($relative in $files) {
    $path = Join-Path $ArtifactDirectory $relative
    if (-not (Test-Path -LiteralPath $path) -or (Get-Item -LiteralPath $path).Length -eq 0) {
      throw "Missing runtime file: $relative"
    }
    $hashes[$relative] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  $contract = Join-Path $root 'modules/abi/include/krypton_ui/abi.h'
  [ordered]@{
    abi_sha256 = (Get-FileHash -LiteralPath $contract -Algorithm SHA256).Hash.ToLowerInvariant()
    files = $hashes
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'runtime-manifest.json') -Encoding utf8NoBOM
  Copy-Item -LiteralPath $contract -Destination (Join-Path $ArtifactDirectory 'abi.h')
  $commit = & git rev-parse HEAD
  if ($LASTEXITCODE -ne 0) { throw 'Could not read UI source revision.' }
  [ordered]@{
    repository = 'perpetus/KryptonUI'
    commit = $commit
    ninja = $ninjaVersion
    configuration = 'Release'
    sdk_configuration_sha256 = (Get-FileHash -LiteralPath 'cmake/WindowsAppSDK.cmake' -Algorithm SHA256).Hash.ToLowerInvariant()
  } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $ArtifactDirectory 'build-info.json') -Encoding utf8NoBOM
}
finally { Pop-Location }
