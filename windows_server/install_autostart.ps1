$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Project = Split-Path -Parent $Root
$TaskName = 'PCMonitorX Monitoring Server'
$Pythonw = Join-Path $Project '.venv\Scripts\pythonw.exe'
if (-not (Test-Path $Pythonw)) { throw "Run windows_server\install.ps1 (or Install.bat) first; $Pythonw not found." }

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
Write-Host "This will create a scheduled task named '$TaskName' that starts the server at log-on,"
Write-Host 'with no window (pythonw.exe), so nothing appears at start-up or blocks shutdown.'
if ($isAdmin) {
    Write-Host 'Running as administrator: the task gets admin rights, which CPU temperature needs (LibreHardwareMonitor).'
} else {
    Write-Host 'Not running as administrator: the server will work, but CPU temperature will be missing.'
    Write-Host 'For CPU temperature, re-run this from an administrator PowerShell.'
}
$answer = Read-Host 'Type CREATE to continue'
if ($answer -cne 'CREATE') { Write-Host 'Cancelled; no task was created.'; exit 0 }

$user = [System.Security.Principal.WindowsIdentity]::GetCurrent().Name
$action = New-ScheduledTaskAction -Execute $Pythonw -Argument "`"$(Join-Path $Root 'app.py')`"" -WorkingDirectory $Project
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $user
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries   # no 3-day limit; keep running on laptops
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel $(if ($isAdmin) { 'Highest' } else { 'Limited' })
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings -Principal $principal `
    -Description 'Starts the local PCMonitorX hardware monitoring API at user log-on (no window).' -Force | Out-Null
Write-Host "Created '$TaskName'. Remove it with: Unregister-ScheduledTask -TaskName '$TaskName' -Confirm:`$false"
