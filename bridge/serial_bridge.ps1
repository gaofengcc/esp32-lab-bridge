param(
  [Parameter(Mandatory = $true)]
  [string]$Port,

  [Parameter(Mandatory = $true)]
  [int]$BaudRate
)

$ErrorActionPreference = "Stop"

# Node.js 按 UTF-8 解码桥接输出；显式设置编码，避免 Win10 默认代码页导致中文错误乱码。
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = $utf8NoBom
$OutputEncoding = $utf8NoBom

# 每次输出后立即 flush：.NET 的 Console.Out 在重定向到管道时默认带缓冲，
# 会把串口日志攒批后才吐给 Node（表现为日志不实时 / 关掉连接才一次性出现）。
function Write-Out([string]$Text) {
  [Console]::Out.WriteLine($Text)
  [Console]::Out.Flush()
}

function Emit([string]$Kind, [string]$Payload = "") {
  if ([string]::IsNullOrWhiteSpace($Payload)) {
    Write-Out $Kind
  } else {
    Write-Out "$Kind $Payload"
  }
}

$serial = $null

try {
  $serial = New-Object System.IO.Ports.SerialPort $Port, $BaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
  $serial.Handshake = [System.IO.Ports.Handshake]::None
  $serial.DtrEnable = $false
  $serial.RtsEnable = $false
  $serial.ReadTimeout = 100
  $serial.WriteTimeout = 1000
  $serial.Open()

  $subscription = Register-ObjectEvent -InputObject $serial -EventName DataReceived -Action {
    try {
      $sender = $Event.Sender
      while ($sender -and $sender.IsOpen -and $sender.BytesToRead -gt 0) {
        $count = [Math]::Min($sender.BytesToRead, 4096)
        $buffer = New-Object byte[] $count
        $read = $sender.Read($buffer, 0, $count)
        if ($read -gt 0) {
          $b64 = [Convert]::ToBase64String($buffer, 0, $read)
          [Console]::Out.WriteLine("DATA $b64"); [Console]::Out.Flush()
        } else {
          break
        }
      }
    } catch {
      [Console]::Out.WriteLine("ERROR $($_.Exception.Message)"); [Console]::Out.Flush()
    }
  }

  Emit "READY"
  Emit "INFO" "$Port $BaudRate"

  while (($line = [Console]::In.ReadLine()) -ne $null) {
    if ($line.Length -eq 0) {
      continue
    }

    if ($line -eq "PING") {
      Emit "PONG"
      continue
    }

    if ($line.StartsWith("WRITE ")) {
      $payload = $line.Substring(6)
      $bytes = [Convert]::FromBase64String($payload)
      $serial.Write($bytes, 0, $bytes.Length)
      Emit "OK"
      continue
    }

    if ($line -eq "RESET") {
      try {
        $serial.DtrEnable = $false
        $serial.RtsEnable = $false
        Start-Sleep -Milliseconds 120
        $serial.DtrEnable = $true
        $serial.RtsEnable = $true
        Start-Sleep -Milliseconds 120
        Emit "OK"
      } catch {
        Emit "ERROR" $_.Exception.Message
      }
      continue
    }

    if ($line -eq "CLOSE") {
      Emit "CLOSING"
      break
    }

    if ($line -eq "EXIT") {
      Emit "BYE"
      break
    }

    Emit "ERROR" "unknown command"
  }
}
catch {
  Emit "ERROR" $_.Exception.Message
}
finally {
  try {
    if ($serial -and $serial.IsOpen) {
      $serial.Close()
    }
  } catch {}
}
