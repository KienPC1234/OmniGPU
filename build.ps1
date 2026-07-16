param(
    [bool]$Build32 = $true,
    [switch]$SkipBuild = $false,
    [switch]$Install = $false,
    [string]$HostAddr = "",
    [int]$HostPort = 9443
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

& "$ScriptDir\scripts\windows\build-and-package.ps1" -Build32 $Build32 -SkipBuild:$SkipBuild -Install:$Install -HostAddr $HostAddr -HostPort $HostPort
