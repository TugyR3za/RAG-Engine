[CmdletBinding()]
param(
    [switch] $SmokeTest,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $SandboxArguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDirectory = Join-Path $repoRoot 'build\skybox'
$configuration = 'Debug'
$cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
$executablePath = Join-Path $buildDirectory "$configuration\RagSandbox.exe"

if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    $configureArguments = @('-S', $repoRoot, '-B', $buildDirectory)
    $dependencyRoot = Join-Path $repoRoot 'build-vs2026\_deps'
    $dependencySources = [ordered]@{
        IMGUI = Join-Path $dependencyRoot 'imgui-src'
        IMGUIZMO = Join-Path $dependencyRoot 'imguizmo-src'
        JOLTPHYSICS = Join-Path $dependencyRoot 'joltphysics-src'
    }

    $allDependencySourcesAvailable = $true
    foreach ($dependencyPath in $dependencySources.Values) {
        if (-not (Test-Path -LiteralPath $dependencyPath -PathType Container)) {
            $allDependencySourcesAvailable = $false
            break
        }
    }

    if ($allDependencySourcesAvailable) {
        Write-Host '[RAG] Configuring with retained offline dependency sources in build-vs2026\_deps.'
        $configureArguments += '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'
        foreach ($entry in $dependencySources.GetEnumerator()) {
            $cmakePath = $entry.Value.Replace('\', '/')
            $configureArguments += "-DFETCHCONTENT_SOURCE_DIR_$($entry.Key)=$cmakePath"
        }
    }
    else {
        Write-Host '[RAG] Offline dependency sources are incomplete; CMake may need network access.'
    }

    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}

Write-Host "[RAG] Building $configuration in $buildDirectory"
& cmake --build $buildDirectory --config $configuration --target RagSandbox --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE; RagSandbox will not be launched."
}

if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Build succeeded but the canonical executable was not found: $executablePath"
}

$executable = Get-Item -LiteralPath $executablePath
$gitCommit = (& git -C $repoRoot rev-parse --short HEAD 2>$null)
if ($LASTEXITCODE -ne 0) {
    $gitCommit = 'unavailable'
}

$launchArguments = @()
if ($SmokeTest) {
    $launchArguments += '--smoke-test'
}
if ($null -ne $SandboxArguments) {
    $launchArguments += $SandboxArguments
}

Write-Host "[RAG] Executable:        $executablePath"
Write-Host "[RAG] Working directory: $repoRoot"
Write-Host "[RAG] Build:             $configuration, commit $gitCommit, $($executable.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"

Push-Location -LiteralPath $repoRoot
try {
    & $executablePath @launchArguments
    $sandboxExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

exit $sandboxExitCode
