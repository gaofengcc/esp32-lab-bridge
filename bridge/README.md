# 串口桥

这是 Win10 侧本地串口代理模板。

## 作用

- 打开 ESP32 串口
- 转发日志
- 执行烧录
- 发起截图和重启

## 配置

编辑 [config.json](./config.json)：

- `projectName`：项目名
- `host` / `port`：Web 服务监听地址和端口
- `defaultPort` / `defaultBaudrate`：默认串口参数。`defaultPort` 推荐设为
  `"auto"`：启动会枚举当前主机串口，跳过已被本桥占用的端口，选择第一个可用端口；
  也可以填写固定端口名（例如 `"COM6"`）。
- `defaultChip`：烧录芯片型号
- `logsDir` / `capturesDir`：日志和截图目录
- `esptoolExe`：可选的 `esptool.exe` 路径

## 启动

运行环境是 Windows 10/Windows，先确保已安装 Node.js，且 `powershell.exe` 可用。

```bash
cd bridge
npm start
```

`host: "0.0.0.0"` 只是监听地址；浏览器或技能里填写的服务地址应使用实际可访问的主机名或 IP，例如 `http://127.0.0.1:3000`。

默认配置中的 `defaultPort: "auto"` 不会预先打开串口；在省略
`POST /api/session/start` 的 `port` 时才执行自动发现。成功后，实际选中的端口会
出现在 `GET /api/state` 的 `port` 字段中。若没有可用串口、枚举脚本失败或枚举超时，
启动请求会立即返回错误，不会把会话停留在 `opening` 状态。

## 接口

- `GET /api/state`
- `GET /api/ports`
- `GET /api/tools`
- `GET /api/events`
- `POST /api/session/start`
- `POST /api/session/stop`
- `POST /api/flash`
- `POST /api/screenshot`
- `POST /api/reboot`
- `POST /api/log/pause`
- `POST /api/log/resume`

`GET /api/ports` 返回当前主机可用串口及友好设备描述。每项包含端口名
（`port`）、描述（`description`，系统无法提供时降级为端口名）和是否已被本桥占用
（`inUse`）。例如：

```json
{
  "ok": true,
  "ports": [
    {
      "port": "COM6",
      "description": "USB-SERIAL CH340 (COM6)",
      "inUse": false
    }
  ]
}
```

当 `defaultPort` 为 `"auto"` 时，`POST /api/session/start` 省略 `port` 会使用
枚举结果中第一个未被本桥占用的端口；实际解析后的端口可通过 `GET /api/state` 的
`port` 字段查看。也可以先调用 `GET /api/ports` 查看每项的 `inUse` 状态。

`list_ports.ps1` 和 `serial_bridge.ps1` 会将脚本输出编码固定为 UTF-8（无 BOM），
Node.js 也按 UTF-8 解码串口桥接消息；因此设备或 PowerShell 的中文错误应保持可读。
