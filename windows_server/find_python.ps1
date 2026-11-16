# Shared helper: returns the command array for a Python 3.10+ interpreter.
# Prefers the py launcher, then python.exe (including the Microsoft Store build).
function Get-PCMonitorXPython {
    $candidates = @()
    if (Get-Command py -ErrorAction SilentlyContinue) { $candidates += ,@('py', '-3') }
    if (Get-Command python -ErrorAction SilentlyContinue) { $candidates += ,@('python') }
    foreach ($cmd in $candidates) {
        $exe = $cmd[0]
        $extra = @()
        if ($cmd.Count -gt 1) { $extra = $cmd[1..($cmd.Count - 1)] }
        try {
            $ver = & $exe @extra -c "import sys; print('.'.join(map(str, sys.version_info[:3])))" 2>$null
            if ($LASTEXITCODE -eq 0 -and $ver -and [version]$ver -ge [version]'3.10') { return ,$cmd }
        } catch { continue }
    }
    throw 'Python 3.10+ was not found (tried "py -3" and "python"). Install Python for the current user, then rerun.'
}
