#requires -Version 5.1 -Assembly System.Windows.Forms,System.Drawing,System.Management.Automation

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# ─────────────────────────────────────────────────────────
# OmniGPU Manager — Windows GUI for Host & Guest control
# ─────────────────────────────────────────────────────────

$script:rootDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:binDir = Join-Path (Join-Path (Join-Path $rootDir "build") "release") "bin"
$script:hostExe = Join-Path $binDir "omnigpu_host.exe"
$script:guestDll = Join-Path $binDir "omnigpu_guest.dll"
$script:icdJson = Join-Path $binDir "vk_icd.json"
$script:hostPort = 9443
$script:timer = $null
$script:hostProcess = $null
$script:logBuffer = [System.Collections.ArrayList]::new()

# ─────────────────────────────────────────────────────────
# Helper Functions
# ─────────────────────────────────────────────────────────
function Update-HostStatus {
    $proc = Get-Process -Name "omnigpu_host" -ErrorAction SilentlyContinue
    $listener = Get-NetTCPConnection -LocalPort $script:hostPort -ErrorAction SilentlyContinue | Where-Object State -eq Listen
    $sessions = Get-NetTCPConnection -LocalPort $script:hostPort -ErrorAction SilentlyContinue | Where-Object State -eq Established

    if ($proc -and $listener) {
        $script:lblHostStatus.Text = "RUNNING"
        $script:lblHostStatus.ForeColor = [Drawing.Color]::LimeGreen
        $script:btnStartHost.Enabled = $false
        $script:btnStopHost.Enabled = $true
        $script:lblHostPid.Text = "PID: $($proc.Id)"
        $script:lblHostUptime.Text = "Started: $($proc.StartTime.ToShortTimeString())"
        $script:lblHostSessions.Text = "Sessions: $($sessions.Count)"
    } else {
        $script:lblHostStatus.Text = "STOPPED"
        $script:lblHostStatus.ForeColor = [Drawing.Color]::Gray
        $script:btnStartHost.Enabled = $true
        $script:btnStopHost.Enabled = $false
        $script:lblHostPid.Text = "PID: --"
        $script:lblHostUptime.Text = "Started: --"
        $script:lblHostSessions.Text = "Sessions: 0"
    }

    # GPU info
    $gpus = Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue
    $script:lblGpuInfo.Text = ""
    foreach ($gpu in $gpus) {
        $vram = if ($gpu.AdapterRAM) { "$([math]::Round($gpu.AdapterRAM/1GB, 1)) GB" } else { "N/A" }
        $script:lblGpuInfo.Text += "$($gpu.Caption) ($vram)`r`n"
    }
}

function Update-GuestStatus {
    $dllOk = Test-Path $script:guestDll
    $icdOk = Test-Path $script:icdJson
    $zinkOk = Test-Path (Join-Path $binDir "opengl32.dll")
    $hostOk = Test-Path $script:hostExe

    $script:lblGuestDll.Text = if ($dllOk) { "omnigpu_guest.dll: OK" } else { "MISSING" }
    $script:lblGuestDll.ForeColor = if ($dllOk) { [Drawing.Color]::LimeGreen } else { [Drawing.Color]::Tomato }
    $script:lblGuestIcd.Text = if ($icdOk) { "vk_icd.json: OK" } else { "MISSING" }
    $script:lblGuestIcd.ForeColor = if ($icdOk) { [Drawing.Color]::LimeGreen } else { [Drawing.Color]::Tomato }
    $script:lblGuestZink.Text = if ($zinkOk) { "Zink: OK" } else { "Missing" }
    $script:lblGuestZink.ForeColor = if ($zinkOk) { [Drawing.Color]::LimeGreen } else { [Drawing.Color]::Gray }
    $script:lblGuestHostExe.Text = if ($hostOk) { "Host EXE: OK" } else { "MISSING" }
    $script:lblGuestHostExe.ForeColor = if ($hostOk) { [Drawing.Color]::LimeGreen } else { [Drawing.Color]::Tomato }

    $script:lblBinDir.Text = "Bin: $($binDir)"
}

function Add-LogMessage {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "HH:mm:ss"
    $line = "[$timestamp] [$Level] $Message"
    $script:logBuffer.Add($line) | Out-Null
    if ($script:logBuffer.Count -gt 1000) { $script:logBuffer.RemoveAt(0) }
    if ($script:txtLog.InvokeRequired) {
        $script:txtLog.Invoke([Action]{ Add-LogMessage @PSBoundParameters }) | Out-Null
        return
    }
    $script:txtLog.AppendText("$line`r`n")
    $script:txtLog.SelectionStart = $script:txtLog.Text.Length
    $script:txtLog.ScrollToCaret()
}

function Start-HostServer {
    if (-not (Test-Path $script:hostExe)) {
        [System.Windows.Forms.MessageBox]::Show("Host executable not found: $($script:hostExe)", "Error", "OK", "Error")
        return
    }
    try {
        Stop-Process -Name "omnigpu_host" -Force -ErrorAction SilentlyContinue
        Start-Sleep 1
        $script:hostProcess = [System.Diagnostics.Process]::Start($script:hostExe)
        Start-Sleep 2
        Add-LogMessage "Host server started (PID: $($script:hostProcess.Id))" "INFO"
        Update-HostStatus
        Add-LogMessage "Host listening on port $($script:hostPort)" "INFO"
    } catch {
        Add-LogMessage "Failed to start host: $_" "ERROR"
    }
}

function Stop-HostServer {
    try {
        Stop-Process -Name "omnigpu_host" -Force -ErrorAction SilentlyContinue
        Add-LogMessage "Host server stopped" "INFO"
        Update-HostStatus
    } catch {
        Add-LogMessage "Failed to stop host: $_" "ERROR"
    }
}

function Deploy-ToVM {
    $vmHost = $script:txtVmHost.Text.Trim()
    $vmUser = $script:txtVmUser.Text.Trim()
    $vmPass = $script:txtVmPass.Text
    $vmDir = $script:txtVmDir.Text.Trim()

    if (-not $vmHost -or -not $vmUser) {
        [System.Windows.Forms.MessageBox]::Show("Enter VM host and user first", "Warning", "OK", "Warning")
        return
    }

    Add-LogMessage "Deploying to $vmUser@$vmHost ..." "INFO"

    $askpass = Join-Path (Split-Path -Parent $PSScriptRoot) "scripts\windows\askpass.bat"
    $env:SSH_ASKPASS = $askpass
    $env:DISPLAY = "dummy"
    $env:SSH_ASKPASS_REQUIRE = "force"
    $sshOpts = "-o StrictHostKeyChecking=no"
    $base = "${vmUser}@${vmHost}"

    $files = @("omnigpu_guest.dll", "omnigpu_guest_test.exe", "vk_icd.json")
    $zinkDll = Join-Path $binDir "opengl32.dll"
    if (Test-Path $zinkDll) { $files += "opengl32.dll" }

    $success = $true
    foreach ($f in $files) {
        $local = Join-Path $binDir $f
        if (Test-Path $local) {
            Add-LogMessage "  Copying $f ..." "INFO"
            $r = scp $sshOpts $local "${base}:${vmDir}\" 2>&1
            if ($LASTEXITCODE -ne 0) {
                Add-LogMessage "  FAILED: $r" "ERROR"
                $success = $false
            }
        }
    }

    # Copy MSVC runtimes too
    foreach ($dll in @("vcruntime140.dll","vcruntime140_1.dll","msvcp140.dll")) {
        $p = Join-Path $binDir $dll
        if (-not (Test-Path $p)) { $p = Join-Path $env:SystemRoot "System32" $dll }
        if (Test-Path $p) {
            scp $sshOpts $p "${base}:${vmDir}\" 2>&1 | Out-Null
        }
    }

    if ($success) {
        Add-LogMessage "Deploy completed successfully" "INFO"
    } else {
        Add-LogMessage "Deploy had errors (check above)" "WARN"
    }
}

function Launch-Guest {
    $vmHost = $script:txtVmHost.Text.Trim()
    $vmUser = $script:txtVmUser.Text.Trim()
    $vmDir = $script:txtVmDir.Text.Trim()
    $targetExe = $script:txtTargetExe.Text.Trim()

    if (-not $vmHost -or -not $vmUser -or -not $vmDir) {
        [System.Windows.Forms.MessageBox]::Show("Fill VM details first", "Warning", "OK", "Warning")
        return
    }

    $askpass = Join-Path (Split-Path -Parent $PSScriptRoot) "scripts\windows\askpass.bat"
    $env:SSH_ASKPASS = $askpass
    $env:DISPLAY = "dummy"
    $env:SSH_ASKPASS_REQUIRE = "force"
    $sshOpts = "-o StrictHostKeyChecking=no"
    $base = "${vmUser}@${vmHost}"

    # Set env vars and launch
    $icdPath = "${vmDir}\vk_icd.json"
    $hostAddr = $script:txtHostAddr.Text.Trim()

    if ($targetExe) {
        Add-LogMessage "Launching $targetExe on $vmHost ..." "INFO"
        $cmd = "set VK_ICD_FILENAMES=${icdPath} && cd ${vmDir} && start `"`" `"${targetExe}`""
        $r = ssh $sshOpts "$base" "powershell -Command `"$cmd`"" 2>&1
        Add-LogMessage "Launch result: $r" "INFO"
    } else {
        Add-LogMessage "Launching guest test (connectivity check) ..." "INFO"
        $r = ssh $sshOpts "$base" "powershell -ExecutionPolicy Bypass -File C:\Users\${vmUser}\Downloads\test_guest.ps1" 2>&1
        Add-LogMessage "Test output saved to log" "INFO"
    }
}

function Run-Diagnostics {
    $diagScript = Join-Path (Split-Path -Parent $PSScriptRoot) "scripts\windows\diagnose.ps1"
    if (-not (Test-Path $diagScript)) {
        Add-LogMessage "Diagnostics script not found: $diagScript" "ERROR"
        return
    }
    Add-LogMessage "Running diagnostics..." "INFO"
    $output = powershell -File $diagScript 2>&1
    foreach ($line in $output) {
        Add-LogMessage $line "DIAG"
    }
    Add-LogMessage "Diagnostics complete" "INFO"
}

# ─────────────────────────────────────────────────────────
# Build GUI
# ─────────────────────────────────────────────────────────
$form = New-Object System.Windows.Forms.Form
$form.Text = "OmniGPU Manager v0.1"
$form.Size = [Drawing.Size]::new(960, 680)
$form.StartPosition = "CenterScreen"
$form.Icon = [System.Drawing.Icon]::ExtractAssociatedIcon($script:hostExe)
$form.Font = [Drawing.Font]::new("Segoe UI", 9)

$tabs = New-Object System.Windows.Forms.TabControl
$tabs.Dock = "Fill"

# ── TAB 1: Host ──
$tabHost = New-Object System.Windows.Forms.TabPage
$tabHost.Text = "Host"
$tabHost.BackColor = [Drawing.Color]::FromArgb(30, 30, 30)

$fontBig = [Drawing.Font]::new("Segoe UI", 14, [Drawing.FontStyle]::Bold)
$fontNormal = [Drawing.Font]::new("Segoe UI", 10)

$lblHostTitle = New-Object System.Windows.Forms.Label
$lblHostTitle.Text = "Host Server"
$lblHostTitle.Font = $fontBig
$lblHostTitle.ForeColor = [Drawing.Color]::White
$lblHostTitle.Location = [Drawing.Point]::new(15, 15)
$lblHostTitle.Size = [Drawing.Size]::new(300, 30)

$script:lblHostStatus = New-Object System.Windows.Forms.Label
$script:lblHostStatus.Text = "CHECKING..."
$script:lblHostStatus.Font = $fontNormal
$script:lblHostStatus.ForeColor = [Drawing.Color]::Gray
$script:lblHostStatus.Location = [Drawing.Point]::new(15, 55)
$script:lblHostStatus.Size = [Drawing.Size]::new(200, 25)

$script:lblHostPid = New-Object System.Windows.Forms.Label
$script:lblHostPid.ForeColor = [Drawing.Color]::White
$script:lblHostPid.Location = [Drawing.Point]::new(15, 85)
$script:lblHostPid.Size = [Drawing.Size]::new(200, 20)

$script:lblHostUptime = New-Object System.Windows.Forms.Label
$script:lblHostUptime.ForeColor = [Drawing.Color]::White
$script:lblHostUptime.Location = [Drawing.Point]::new(15, 110)
$script:lblHostUptime.Size = [Drawing.Size]::new(200, 20)

$script:lblHostSessions = New-Object System.Windows.Forms.Label
$script:lblHostSessions.ForeColor = [Drawing.Color]::White
$script:lblHostSessions.Location = [Drawing.Point]::new(15, 135)
$script:lblHostSessions.Size = [Drawing.Size]::new(200, 20)

$script:btnStartHost = New-Object System.Windows.Forms.Button
$script:btnStartHost.Text = "Start Host"
$script:btnStartHost.Location = [Drawing.Point]::new(15, 170)
$script:btnStartHost.Size = [Drawing.Size]::new(120, 32)
$script:btnStartHost.BackColor = [Drawing.Color]::FromArgb(0, 120, 215)
$script:btnStartHost.ForeColor = [Drawing.Color]::White
$script:btnStartHost.Add_Click({ Start-HostServer })

$script:btnStopHost = New-Object System.Windows.Forms.Button
$script:btnStopHost.Text = "Stop Host"
$script:btnStopHost.Location = [Drawing.Point]::new(145, 170)
$script:btnStopHost.Size = [Drawing.Size]::new(120, 32)
$script:btnStopHost.BackColor = [Drawing.Color]::FromArgb(200, 50, 50)
$script:btnStopHost.ForeColor = [Drawing.Color]::White
$script:btnStopHost.Enabled = $false
$script:btnStopHost.Add_Click({ Stop-HostServer })

$grpGpu = New-Object System.Windows.Forms.GroupBox
$grpGpu.Text = "GPU Information"
$grpGpu.ForeColor = [Drawing.Color]::White
$grpGpu.Location = [Drawing.Point]::new(15, 220)
$grpGpu.Size = [Drawing.Size]::new(420, 150)

$script:lblGpuInfo = New-Object System.Windows.Forms.Label
$script:lblGpuInfo.ForeColor = [Drawing.Color]::LightGray
$script:lblGpuInfo.Location = [Drawing.Point]::new(10, 25)
$script:lblGpuInfo.Size = [Drawing.Size]::new(400, 120)

$grpGpu.Controls.Add($script:lblGpuInfo)

$tabHost.Controls.AddRange(@($lblHostTitle, $script:lblHostStatus, $script:lblHostPid, $script:lblHostUptime, $script:lblHostSessions, $script:btnStartHost, $script:btnStopHost, $grpGpu))

# ── TAB 2: Guest ──
$tabGuest = New-Object System.Windows.Forms.TabPage
$tabGuest.Text = "Guest"
$tabGuest.BackColor = [Drawing.Color]::FromArgb(30, 30, 30)

$lblGuestTitle = New-Object System.Windows.Forms.Label
$lblGuestTitle.Text = "Guest VM Management"
$lblGuestTitle.Font = $fontBig
$lblGuestTitle.ForeColor = [Drawing.Color]::White
$lblGuestTitle.Location = [Drawing.Point]::new(15, 15)
$lblGuestTitle.Size = [Drawing.Size]::new(350, 30)

# Guest files status
$grpFiles = New-Object System.Windows.Forms.GroupBox
$grpFiles.Text = "Build Artifacts"
$grpFiles.ForeColor = [Drawing.Color]::White
$grpFiles.Location = [Drawing.Point]::new(15, 55)
$grpFiles.Size = [Drawing.Size]::new(420, 140)

$script:lblBinDir = New-Object System.Windows.Forms.Label
$script:lblBinDir.ForeColor = [Drawing.Color]::LightGray
$script:lblBinDir.Location = [Drawing.Point]::new(10, 20)
$script:lblBinDir.Size = [Drawing.Size]::new(400, 18)

$script:lblGuestDll = New-Object System.Windows.Forms.Label
$script:lblGuestDll.Location = [Drawing.Point]::new(10, 42)
$script:lblGuestDll.Size = [Drawing.Size]::new(200, 18)

$script:lblGuestIcd = New-Object System.Windows.Forms.Label
$script:lblGuestIcd.Location = [Drawing.Point]::new(10, 64)
$script:lblGuestIcd.Size = [Drawing.Size]::new(200, 18)

$script:lblGuestZink = New-Object System.Windows.Forms.Label
$script:lblGuestZink.Location = [Drawing.Point]::new(10, 86)
$script:lblGuestZink.Size = [Drawing.Size]::new(200, 18)

$script:lblGuestHostExe = New-Object System.Windows.Forms.Label
$script:lblGuestHostExe.Location = [Drawing.Point]::new(10, 108)
$script:lblGuestHostExe.Size = [Drawing.Size]::new(200, 18)

$grpFiles.Controls.AddRange(@($script:lblBinDir, $script:lblGuestDll, $script:lblGuestIcd, $script:lblGuestZink, $script:lblGuestHostExe))

# VM connection details
$grpVm = New-Object System.Windows.Forms.GroupBox
$grpVm.Text = "Remote VM"
$grpVm.ForeColor = [Drawing.Color]::White
$grpVm.Location = [Drawing.Point]::new(15, 205)
$grpVm.Size = [Drawing.Size]::new(420, 180)

$lblVmHost = New-Object System.Windows.Forms.Label
$lblVmHost.Text = "Host:"
$lblVmHost.ForeColor = [Drawing.Color]::White
$lblVmHost.Location = [Drawing.Point]::new(10, 25)
$lblVmHost.Size = [Drawing.Size]::new(40, 22)

$script:txtVmHost = New-Object System.Windows.Forms.TextBox
$script:txtVmHost.Text = "192.168.1.113"
$script:txtVmHost.Location = [Drawing.Point]::new(55, 23)
$script:txtVmHost.Size = [Drawing.Size]::new(120, 22)

$lblVmUser = New-Object System.Windows.Forms.Label
$lblVmUser.Text = "User:"
$lblVmUser.ForeColor = [Drawing.Color]::White
$lblVmUser.Location = [Drawing.Point]::new(185, 25)
$lblVmUser.Size = [Drawing.Size]::new(40, 22)

$script:txtVmUser = New-Object System.Windows.Forms.TextBox
$script:txtVmUser.Text = "test"
$script:txtVmUser.Location = [Drawing.Point]::new(225, 23)
$script:txtVmUser.Size = [Drawing.Size]::new(80, 22)

$lblVmPass = New-Object System.Windows.Forms.Label
$lblVmPass.Text = "Pass:"
$lblVmPass.ForeColor = [Drawing.Color]::White
$lblVmPass.Location = [Drawing.Point]::new(10, 55)
$lblVmPass.Size = [Drawing.Size]::new(40, 22)

$script:txtVmPass = New-Object System.Windows.Forms.MaskedTextBox
$script:txtVmPass.PasswordChar = '*'
$script:txtVmPass.Text = "Htt@123456"
$script:txtVmPass.Location = [Drawing.Point]::new(55, 53)
$script:txtVmPass.Size = [Drawing.Size]::new(120, 22)

$lblVmDir = New-Object System.Windows.Forms.Label
$lblVmDir.Text = "Dir:"
$lblVmDir.ForeColor = [Drawing.Color]::White
$lblVmDir.Location = [Drawing.Point]::new(10, 85)
$lblVmDir.Size = [Drawing.Size]::new(30, 22)

$script:txtVmDir = New-Object System.Windows.Forms.TextBox
$script:txtVmDir.Text = "C:\Users\test\Downloads\GPU_Caps_Viewer_1.64.3.0\GPU_Caps_Viewer"
$script:txtVmDir.Location = [Drawing.Point]::new(45, 83)
$script:txtVmDir.Size = [Drawing.Size]::new(360, 22)

$lblHostAddr = New-Object System.Windows.Forms.Label
$lblHostAddr.Text = "Host Addr:"
$lblHostAddr.ForeColor = [Drawing.Color]::White
$lblHostAddr.Location = [Drawing.Point]::new(10, 115)
$lblHostAddr.Size = [Drawing.Size]::new(70, 22)

$script:txtHostAddr = New-Object System.Windows.Forms.TextBox
$script:txtHostAddr.Text = "192.168.1.239"
$script:txtHostAddr.Location = [Drawing.Point]::new(85, 113)
$script:txtHostAddr.Size = [Drawing.Size]::new(120, 22)

$lblTargetExe = New-Object System.Windows.Forms.Label
$lblTargetExe.Text = "Target EXE:"
$lblTargetExe.ForeColor = [Drawing.Color]::White
$lblTargetExe.Location = [Drawing.Point]::new(10, 145)
$lblTargetExe.Size = [Drawing.Size]::new(70, 22)

$script:txtTargetExe = New-Object System.Windows.Forms.TextBox
$script:txtTargetExe.Text = ""
$script:txtTargetExe.Location = [Drawing.Point]::new(85, 143)
$script:txtTargetExe.Size = [Drawing.Size]::new(320, 22)

$grpVm.Controls.AddRange(@($lblVmHost, $script:txtVmHost, $lblVmUser, $script:txtVmUser, $lblVmPass, $script:txtVmPass, $lblVmDir, $script:txtVmDir, $lblHostAddr, $script:txtHostAddr, $lblTargetExe, $script:txtTargetExe))

# Action buttons
$script:btnDeploy = New-Object System.Windows.Forms.Button
$script:btnDeploy.Text = "Deploy to VM"
$script:btnDeploy.Location = [Drawing.Point]::new(15, 400)
$script:btnDeploy.Size = [Drawing.Size]::new(130, 32)
$script:btnDeploy.BackColor = [Drawing.Color]::FromArgb(0, 120, 215)
$script:btnDeploy.ForeColor = [Drawing.Color]::White
$script:btnDeploy.Add_Click({ Deploy-ToVM })

$script:btnLaunch = New-Object System.Windows.Forms.Button
$script:btnLaunch.Text = "Launch Guest"
$script:btnLaunch.Location = [Drawing.Point]::new(155, 400)
$script:btnLaunch.Size = [Drawing.Size]::new(130, 32)
$script:btnLaunch.BackColor = [Drawing.Color]::FromArgb(0, 180, 80)
$script:btnLaunch.ForeColor = [Drawing.Color]::White
$script:btnLaunch.Add_Click({ Launch-Guest })

$script:btnTestConn = New-Object System.Windows.Forms.Button
$script:btnTestConn.Text = "Test Connection"
$script:btnTestConn.Location = [Drawing.Point]::new(295, 400)
$script:btnTestConn.Size = [Drawing.Size]::new(130, 32)
$script:btnTestConn.Add_Click({
    $script:txtTargetExe.Text = ""
    Launch-Guest
})

$tabGuest.Controls.AddRange(@($lblGuestTitle, $grpFiles, $grpVm, $script:btnDeploy, $script:btnLaunch, $script:btnTestConn))

# ── TAB 3: Logs ──
$tabLog = New-Object System.Windows.Forms.TabPage
$tabLog.Text = "Logs"
$tabLog.BackColor = [Drawing.Color]::FromArgb(30, 30, 30)

$script:txtLog = New-Object System.Windows.Forms.TextBox
$script:txtLog.Multiline = $true
$script:txtLog.ReadOnly = $true
$script:txtLog.ScrollBars = "Vertical"
$script:txtLog.WordWrap = $false
$script:txtLog.BackColor = [Drawing.Color]::FromArgb(15, 15, 15)
$script:txtLog.ForeColor = [Drawing.Color]::LimeGreen
$script:txtLog.Font = [Drawing.Font]::new("Consolas", 9)
$script:txtLog.Dock = "Fill"
$script:txtLog.Location = [Drawing.Point]::new(0, 0)

$btnClearLog = New-Object System.Windows.Forms.Button
$btnClearLog.Text = "Clear"
$btnClearLog.Location = [Drawing.Point]::new(10, 10)
$btnClearLog.Size = [Drawing.Size]::new(80, 25)
$btnClearLog.Add_Click({ $script:txtLog.Clear(); $script:logBuffer.Clear() })

$btnDiag = New-Object System.Windows.Forms.Button
$btnDiag.Text = "Run Diagnostics"
$btnDiag.Location = [Drawing.Point]::new(100, 10)
$btnDiag.Size = [Drawing.Size]::new(120, 25)
$btnDiag.Add_Click({ Run-Diagnostics })

$pnlLogTop = New-Object System.Windows.Forms.Panel
$pnlLogTop.Height = 45
$pnlLogTop.Dock = "Top"
$pnlLogTop.BackColor = [Drawing.Color]::FromArgb(40, 40, 40)
$pnlLogTop.Controls.AddRange(@($btnClearLog, $btnDiag))

$tabLog.Controls.AddRange(@($pnlLogTop, $script:txtLog))

# Add tabs
$tabs.TabPages.Add($tabHost)
$tabs.TabPages.Add($tabGuest)
$tabs.TabPages.Add($tabLog)
$form.Controls.Add($tabs)

# ── Timer for auto-refresh ──
$script:timer = New-Object System.Windows.Forms.Timer
$script:timer.Interval = 3000
$script:timer.Add_Tick({
    Update-HostStatus
    Update-GuestStatus
})
$script:timer.Start()

# ── Form events ──
$form.Add_Shown({
    Add-LogMessage "OmniGPU Manager started" "INFO"
    Add-LogMessage "Root: $script:rootDir" "INFO"
    Add-LogMessage "Bin: $script:binDir" "INFO"
    Update-HostStatus
    Update-GuestStatus
    Add-LogMessage "Ready. Host port: $($script:hostPort)" "INFO"
})

$form.Add_FormClosing({
    $script:timer.Stop()
})

# ── Start ──
[Application]::Run($form)
