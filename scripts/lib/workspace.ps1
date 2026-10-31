# Prepare and validate project storage before running tools.
function Initialize-SevenzipWorkspace([string]$ProjectRoot) {
    $env:PYTHONDONTWRITEBYTECODE = '1'
    $python = if ($env:SEVENZIP_RUNNER_PYTHON) { $env:SEVENZIP_RUNNER_PYTHON } else { 'python' }
    $configuration = & $python -B (Join-Path $ProjectRoot 'scripts/lib/build_paths.py') env --root $ProjectRoot --format json
    if ($LASTEXITCODE -ne 0) { throw 'Build workspace verification failed.' }
    ($configuration | ConvertFrom-Json).PSObject.Properties | ForEach-Object {
        [Environment]::SetEnvironmentVariable($_.Name, [string]$_.Value, 'Process')
    }
}

function Assert-SevenzipOutput([string]$ProjectRoot, [string]$Path) {
    $python = if ($env:SEVENZIP_RUNNER_PYTHON) { $env:SEVENZIP_RUNNER_PYTHON } else { 'python' }
    & $python -B (Join-Path $ProjectRoot 'scripts/lib/build_paths.py') check-output --root $ProjectRoot --output $Path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Output is outside the project workspace.' }
}
