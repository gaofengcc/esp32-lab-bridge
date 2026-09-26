# ESP32 Lab Bridge Win10 启动脚本
# 支持本地路径和 UNC 网络共享路径 (\\server\share\...)

$ErrorActionPreference = "Stop"

$root = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($root)) {
    $root = Split-Path -Parent $MyInvocation.MyCommand.Path
}
Set-Location -LiteralPath $root

function Find-NodeExe {
    $cmd = Get-Command node -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }

    $candidates = @(
        "$env:ProgramFiles\nodejs\node.exe",
        ${env:ProgramFiles(x86)} + "\nodejs\node.exe",
        "$env:LOCALAPPDATA\Programs\nodejs\node.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return $null
}

function Read-BridgePort {
    $configPath = Join-Path $root "config.json"
    if (-not (Test-Path -LiteralPath $configPath)) {
        return 3000
    }
    try {
        $config = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($null -ne $config.port) {
            return [int]$config.port
        }
    } catch {
        Write-Host "[WARN] config.json parse failed, use default port 3000"
    }
    return 3000
}

$nodeExe = Find-NodeExe
if (-not $nodeExe) {
    Write-Host "[ERROR] Node.js not found. Install Node.js 18+ from https://nodejs.org/"
    Read-Host "Press Enter to exit"
    exit 1
}

$serverJs = Join-Path $root "server.js"
if (-not (Test-Path -LiteralPath $serverJs)) {
    Write-Host "[ERROR] server.js not found: $serverJs"
    Read-Host "Press Enter to exit"
    exit 1
}

if (-not (Test-Path -LiteralPath (Join-Path $root "config.json"))) {
    Write-Host "[ERROR] config.json not found: $root"
    Read-Host "Press Enter to exit"
    exit 1
}

$port = Read-BridgePort

Write-Host "ESP32 Lab Bridge"
Write-Host "Work dir : $root"
Write-Host "Node     : $nodeExe"
Write-Host "Web UI   : http://127.0.0.1:$port"
Write-Host "Stop     : close window or Ctrl+C"
Write-Host ""

& $nodeExe $serverJs
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    Write-Host ""
    Write-Host "[ERROR] server exited with code $exitCode"
    Read-Host "Press Enter to exit"
    exit $exitCode
}
