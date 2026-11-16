$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Project = Split-Path -Parent $Root
$Venv = Join-Path $Project '.venv'
. (Join-Path $Root 'find_python.ps1')

$Py = Get-PCMonitorXPython
$PyExe = $Py[0]
$PyArgs = @()
if ($Py.Count -gt 1) { $PyArgs = $Py[1..($Py.Count - 1)] }

$VenvPython = Join-Path $Venv 'Scripts\python.exe'
if (-not (Test-Path $VenvPython)) {
    Write-Host "Creating project virtual environment at $Venv"
    & $PyExe @PyArgs -m venv $Venv
    if ($LASTEXITCODE -ne 0) { throw 'Creating the virtual environment failed.' }
}
& $VenvPython -m pip install --upgrade pip
& $VenvPython -m pip install -r (Join-Path $Project 'requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'pip install failed; see output above.' }
Write-Host "Installed PCMonitorX dependencies into $Venv"
Write-Host 'No system-wide Python packages, firewall rules, or startup tasks were changed.'
