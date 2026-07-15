Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# ---- Main Form ----
$form = New-Object System.Windows.Forms.Form
$form.Text = "OmniGPU Launcher"
$form.Size = New-Object System.Drawing.Size(640,520)
$form.StartPosition = "CenterScreen"
$form.Icon = $null

# ---- Logo / Title ----
$title = New-Object System.Windows.Forms.Label
$title.Text = "OmniGPU Launcher v0.2.0"
$title.Font = New-Object System.Drawing.Font("Segoe UI",14,[System.Drawing.FontStyle]::Bold)
$title.Location = New-Object System.Drawing.Point(12,12)
$title.Size = New-Object System.Drawing.Size(400,30)
$form.Controls.Add($title)

# ---- Browse Panel ----
$browseGroup = New-Object System.Windows.Forms.GroupBox
$browseGroup.Text = "Target Application"
$browseGroup.Location = New-Object System.Drawing.Point(12,50)
$browseGroup.Size = New-Object System.Drawing.Size(600,80)
$form.Controls.Add($browseGroup)

$lblPath = New-Object System.Windows.Forms.Label
$lblPath.Text = "Executable:"
$lblPath.Location = New-Object System.Drawing.Point(10,25)
$lblPath.Size = New-Object System.Drawing.Size(70,25)
$browseGroup.Controls.Add($lblPath)

$txtPath = New-Object System.Windows.Forms.TextBox
$txtPath.Location = New-Object System.Drawing.Point(80,25)
$txtPath.Size = New-Object System.Drawing.Size(410,22)
$browseGroup.Controls.Add($txtPath)

$btnBrowse = New-Object System.Windows.Forms.Button
$btnBrowse.Text = "Browse..."
$btnBrowse.Location = New-Object System.Drawing.Point(500,24)
$btnBrowse.Size = New-Object System.Drawing.Size(85,25)
$browseGroup.Controls.Add($btnBrowse)

$lblArgs = New-Object System.Windows.Forms.Label
$lblArgs.Text = "Args:"
$lblArgs.Location = New-Object System.Drawing.Point(10,55)
$lblArgs.Size = New-Object System.Drawing.Size(70,25)
$browseGroup.Controls.Add($lblArgs)

$txtArgs = New-Object System.Windows.Forms.TextBox
$txtArgs.Location = New-Object System.Drawing.Point(80,55)
$txtArgs.Size = New-Object System.Drawing.Size(410,22)
$browseGroup.Controls.Add($txtArgs)

# ---- Status Panel ----
$statusGroup = New-Object System.Windows.Forms.GroupBox
$statusGroup.Text = "Translation Layers"
$statusGroup.Location = New-Object System.Drawing.Point(12,140)
$statusGroup.Size = New-Object System.Drawing.Size(600,110)
$form.Controls.Add($statusGroup)

function Add-StatusRow($group, $label, $y) {
    $lbl = New-Object System.Windows.Forms.Label
    $lbl.Text = $label
    $lbl.Location = New-Object System.Drawing.Point(10,$y)
    $lbl.Size = New-Object System.Drawing.Size(250,20)
    $group.Controls.Add($lbl)

    $status = New-Object System.Windows.Forms.Label
    $status.Text = "???"
    $status.Location = New-Object System.Drawing.Point(270,$y)
    $status.Size = New-Object System.Drawing.Size(200,20)
    $status.ForeColor = [System.Drawing.Color]::Gray
    $group.Controls.Add($status)
    return $status
}

$statusOmni = Add-StatusRow $statusGroup "OmniGPU Guest (omnigpu_guest.dll)" 20
$statusVulkan = Add-StatusRow $statusGroup "Vulkan Loader (vulkan-1.dll)" 45
$statusZink = Add-StatusRow $statusGroup "Zink OpenGL->Vulkan (opengl32.dll)" 70
$statusCLVK = Add-StatusRow $statusGroup "clvk OpenCL->Vulkan (OpenCL.dll)" 95

# ---- Host Config Panel ----
$hostGroup = New-Object System.Windows.Forms.GroupBox
$hostGroup.Text = "Host Connection"
$hostGroup.Location = New-Object System.Drawing.Point(12,260)
$hostGroup.Size = New-Object System.Drawing.Size(600,70)
$form.Controls.Add($hostGroup)

$lblHost = New-Object System.Windows.Forms.Label
$lblHost.Text = "Host:"
$lblHost.Location = New-Object System.Drawing.Point(10,25)
$lblHost.Size = New-Object System.Drawing.Size(40,25)
$hostGroup.Controls.Add($lblHost)

$txtHost = New-Object System.Windows.Forms.TextBox
$txtHost.Text = "192.168.1.239"
$txtHost.Location = New-Object System.Drawing.Point(50,25)
$txtHost.Size = New-Object System.Drawing.Size(140,22)
$hostGroup.Controls.Add($txtHost)

$lblPort = New-Object System.Windows.Forms.Label
$lblPort.Text = "Port:"
$lblPort.Location = New-Object System.Drawing.Point(200,25)
$lblPort.Size = New-Object System.Drawing.Size(35,25)
$hostGroup.Controls.Add($lblPort)

$txtPort = New-Object System.Windows.Forms.TextBox
$txtPort.Text = "9443"
$txtPort.Location = New-Object System.Drawing.Point(235,25)
$txtPort.Size = New-Object System.Drawing.Size(60,22)
$hostGroup.Controls.Add($txtPort)

# ---- Log Box ----
$logBox = New-Object System.Windows.Forms.TextBox
$logBox.Location = New-Object System.Drawing.Point(12,340)
$logBox.Size = New-Object System.Drawing.Size(600,80)
$logBox.Multiline = $true
$logBox.ScrollBars = "Vertical"
$logBox.ReadOnly = $true
$logBox.BackColor = [System.Drawing.Color]::FromArgb(240,240,240)
$form.Controls.Add($logBox)

function Write-Log($msg) {
    $logBox.AppendText("[$([DateTime]::Now.ToString('HH:mm:ss'))] $msg`r`n")
    $logBox.SelectionStart = $logBox.TextLength
    $logBox.ScrollToCaret()
}

# ---- Buttons ----
$btnLaunch = New-Object System.Windows.Forms.Button
$btnLaunch.Text = "Launch"
$btnLaunch.Location = New-Object System.Drawing.Point(440,435)
$btnLaunch.Size = New-Object System.Drawing.Size(85,30)
$btnLaunch.Enabled = $false
$form.Controls.Add($btnLaunch)

$btnCancel = New-Object System.Windows.Forms.Button
$btnCancel.Text = "Close"
$btnCancel.Location = New-Object System.Drawing.Point(530,435)
$btnCancel.Size = New-Object System.Drawing.Size(85,30)
$form.Controls.Add($btnCancel)

# ---- Script directory (where the DLLs are) ----
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$runtimeDir = if (Test-Path "$scriptDir\omnigpu_guest.dll") { $scriptDir }
              else { Join-Path $scriptDir "..\..\build\release\bin" }

function Update-Status {
    $found = @{}
    $dlls = @{
        "OmniGPU Guest" = "omnigpu_guest.dll"
        "Vulkan Loader" = "vulkan-1.dll"
        "Zink" = "opengl32.dll"
        "clvk" = "OpenCL.dll"
    }

    $allOk = $true
    foreach ($key in $dlls.Keys) {
        $path = Join-Path $runtimeDir $dlls[$key]
        if (Test-Path $path) {
            $size = (Get-Item $path).Length
            $statusText = "OK ($([math]::Round($size/1KB)) KB)"
            $color = [System.Drawing.Color]::Green
        } else {
            $statusText = "MISSING"
            if ($key -eq "Zink" -or $key -eq "clvk") {
                $color = [System.Drawing.Color]::Orange
            } else {
                $color = [System.Drawing.Color]::Red
                $allOk = $false
            }
        }

        switch ($key) {
            "OmniGPU Guest" { $statusOmni.Text = $statusText; $statusOmni.ForeColor = $color }
            "Vulkan Loader" { $statusVulkan.Text = $statusText; $statusVulkan.ForeColor = $color }
            "Zink" { $statusZink.Text = $statusText; $statusZink.ForeColor = $color }
            "clvk" { $statusCLVK.Text = $statusText; $statusCLVK.ForeColor = $color }
        }
    }

    $btnLaunch.Enabled = $allOk -and ($txtPath.Text -ne "")
}

function Invoke-Launch {
    $exePath = $txtPath.Text
    if ($exePath -eq "" -or !(Test-Path $exePath)) {
        Write-Log "ERROR: Invalid executable path"
        return
    }

    $exeDir = Split-Path $exePath -Parent
    $exeName = Split-Path $exePath -Leaf
    $hostAddr = $txtHost.Text
    $hostPort = $txtPort.Text
    $argsStr = $txtArgs.Text

    Write-Log "=== Launching $exeName ==="
    Write-Log "Runtime dir: $runtimeDir"
    Write-Log "Game dir:    $exeDir"
    Write-Log "Host:        $hostAddr`:$hostPort"

    # Deploy DLLs to game directory
    $deployDlls = @("omnigpu_guest.dll", "vulkan-1.dll", "vk_icd.json", "opengl32.dll", "OpenCL.dll")
    foreach ($dll in $deployDlls) {
        $src = Join-Path $runtimeDir $dll
        $dst = Join-Path $exeDir $dll
        if (Test-Path $src) {
            if (!(Test-Path $dst) -or ((Get-Item $src).Length -ne (Get-Item $dst -ErrorAction SilentlyContinue).Length)) {
                Copy-Item $src $dst -Force
                Write-Log "Deployed: $dll"
            }
        }
    }

    # Deploy CRT DLLs
    $crtDlls = @("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")
    $crtPaths = @(
        "$env:SystemRoot\System32",
        "$env:ProgramFiles\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\v143",
        "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC"
    )
    foreach ($dll in $crtDlls) {
        $src = $null
        foreach ($p in $crtPaths) {
            $test = Join-Path $p $dll
            if (Test-Path $test) { $src = $test; break }
        }
        if ($src) {
            $dst = Join-Path $exeDir $dll
            if (!(Test-Path $dst)) {
                Copy-Item $src $dst -Force
                Write-Log "Deployed CRT: $dll"
            }
        } else {
            Write-Log "WARNING: CRT $dll not found on system"
        }
    }

    # Deploy guest config
    $guestCfg = Join-Path $runtimeDir "omnigpu_guest.json"
    if (!(Test-Path $guestCfg)) {
        $guestCfg = Join-Path $scriptDir "omnigpu_guest.json"
    }
    if (Test-Path $guestCfg) {
        $cfg = Get-Content $guestCfg -Raw | ConvertFrom-Json
        $cfg.host = $hostAddr
        $cfg.port = [int]$hostPort
        $cfg | ConvertTo-Json -Compress | Set-Content (Join-Path $exeDir "omnigpu_guest.json") -Force
        Write-Log "Guest config updated: host=$hostAddr`:$hostPort"
    }

    # Set environment and launch
    $icdManifest = Join-Path $exeDir "vk_icd.json"
    $env:VK_ICD_FILENAMES = $icdManifest
    $env:OMNIGPU_HOST = $hostAddr
    $env:OMNIGPU_PORT = $hostPort

    Write-Log "VK_ICD_FILENAMES = $icdManifest"
    Write-Log "Starting: $exePath"

    try {
        $startInfo = New-Object System.Diagnostics.ProcessStartInfo
        $startInfo.FileName = $exePath
        $startInfo.Arguments = $argsStr
        $startInfo.WorkingDirectory = $exeDir
        $startInfo.UseShellExecute = $false
        $startInfo.RedirectStandardOutput = $false
        $startInfo.RedirectStandardError = $false

        $proc = [System.Diagnostics.Process]::Start($startInfo)
        Write-Log "Launched! PID: $($proc.Id)"
        Write-Log "Waiting for exit..."

        # Monitor process
        $timer = New-Object System.Windows.Forms.Timer
        $timer.Interval = 1000
        $timer.Add_Tick({
            if ($proc.HasExited) {
                $timer.Stop()
                Write-Log "Process exited with code $($proc.ExitCode)"
                $btnLaunch.Enabled = $true
            }
        })
        $timer.Start()
        $btnLaunch.Enabled = $false
    } catch {
        Write-Log "ERROR: $($_.Exception.Message)"
    }
}

# ---- Event Handlers ----
$btnBrowse.Add_Click({
    $openFile = New-Object System.Windows.Forms.OpenFileDialog
    $openFile.Filter = "Executables (*.exe)|*.exe|All Files (*.*)|*.*"
    $openFile.Title = "Select Application to Launch"
    if ($openFile.ShowDialog() -eq "OK") {
        $txtPath.Text = $openFile.FileName
        Update-Status
    }
})

$txtPath.Add_TextChanged({ Update-Status })

$btnLaunch.Add_Click({ Invoke-Launch })
$btnCancel.Add_Click({ $form.Close() })

# ---- Initialize ----
Update-Status
Write-Log "OmniGPU Launcher ready."
Write-Log "Runtime directory: $runtimeDir"
if ((Get-Item $runtimeDir -ErrorAction SilentlyContinue) -and (Test-Path (Join-Path $runtimeDir "omnigpu_guest.dll"))) {
    Write-Log "All files found. Select an EXE and click Launch."
} else {
    Write-Log "WARNING: Runtime DLLs not found in $runtimeDir"
    Write-Log "Place this script next to omnigpu_guest.dll or in tools/windows/"
}

# ---- Show Form ----
[void]$form.ShowDialog()
