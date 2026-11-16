$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Project = Split-Path -Parent $Root
$TaskName = 'PCMonitorX Monitoring Server'
$StartScript = Join-Path $Root 'start_server.ps1'

Write-Host "This will create a per-user scheduled task named '$TaskName' that starts at logon."
$answer = Read-Host 'Type CREATE to continue'
if ($answer -cne 'CREATE') { Write-Host 'Cancelled; no task was created.'; exit 0 }

$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$StartScript`"" -WorkingDirectory $Project
$trigger = New-ScheduledTaskTrigger -AtLogOn -User ([System.Security.Principal.WindowsIdentity]::GetCurrent().Name)
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings -Description 'Starts the local PCMonitorX hardware monitoring API at user logon.' -Force | Out-Null
Write-Host "Created '$TaskName'. Remove it with: Unregister-ScheduledTask -TaskName '$TaskName' -Confirm:`$false"
