$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Project = Split-Path -Parent $Root
$VenvPython = Join-Path $Project '.venv\Scripts\python.exe'
if (-not (Test-Path $VenvPython)) {
    throw "Project virtual environment not found. Run windows_server\install.ps1 first."
}
& $VenvPython (Join-Path $Root 'app.py')
