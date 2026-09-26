# 输出本机串口列表。脚本只输出一行 UTF-8 JSON，便于 Node.js 可靠解析。
$ErrorActionPreference = "Stop"

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = $utf8NoBom
$OutputEncoding = $utf8NoBom

$portNames = @()
try {
  $portNames = @([System.IO.Ports.SerialPort]::GetPortNames())
} catch {
  # 某些精简环境可能没有可用的 SerialPort 实现；继续尝试从 PnP 信息补全。
  $portNames = @()
}

$descriptions = @{}
$pnpPortNames = @()
try {
  $pnpItems = @(Get-CimInstance Win32_PnPEntity -ErrorAction Stop)
  foreach ($item in $pnpItems) {
    $friendlyName = [string]$item.Name
    if ([string]::IsNullOrWhiteSpace($friendlyName)) {
      continue
    }

    $matches = [regex]::Matches($friendlyName, "(?i)\bCOM\d+\b")
    foreach ($match in $matches) {
      $name = $match.Value.ToUpperInvariant()
      $pnpPortNames += $name
      if (-not $descriptions.ContainsKey($name)) {
        $descriptions[$name] = $friendlyName
      }
    }
  }
} catch {
  # 没有 CIM/WMI 权限时仍返回端口号，不让枚举接口整体失败。
  $descriptions = @{}
  $pnpPortNames = @()
}

$allNames = @($portNames + $pnpPortNames) |
  ForEach-Object { ([string]$_).Trim().ToUpperInvariant() } |
  Where-Object { $_ -match "^COM\d+$" } |
  Sort-Object { [int]($_ -replace "^COM", "") } -Unique

$result = @(
  foreach ($name in $allNames) {
    $description = if ($descriptions.ContainsKey($name)) {
      [string]$descriptions[$name]
    } else {
      $name
    }
    [PSCustomObject]@{
      port = $name
      description = if ([string]::IsNullOrWhiteSpace($description)) { $name } else { $description }
    }
  }
)

Write-Output (ConvertTo-Json -InputObject @($result) -Compress)
