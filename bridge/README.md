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
- `host` / `port`：Web 服务监听地址
- `defaultPort` / `defaultBaudrate`：默认串口参数
- `defaultChip`：烧录芯片型号
- `logsDir` / `capturesDir`：日志和截图目录
- `esptoolExe`：可选的 `esptool.exe` 路径

## 启动

```bash
cd bridge
npm start
```

## 接口

- `GET /api/state`
- `GET /api/tools`
- `GET /api/events`
- `POST /api/session/start`
- `POST /api/session/stop`
- `POST /api/flash`
- `POST /api/screenshot`
- `POST /api/reboot`
- `POST /api/log/pause`
- `POST /api/log/resume`

