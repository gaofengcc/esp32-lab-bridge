# 接入示例

下面用一个示例项目说明三步接入流程。这里的 `Nas-assistant` 只是示例名，实际使用时请替换成你的项目。

## 第 1 步 复制固件组件

把 `firmware/cdc_command`、`firmware/log_gate`、`firmware/lvgl_screenshot` 复制到目标 ESP32 项目。

然后在目标项目里加入组件依赖，例如：

```yaml
dependencies:
  lvgl/lvgl: "^9.5.0"
```

或者在 `CMakeLists.txt` 里声明 `REQUIRES`。

## 第 2 步 配置桥接服务

复制 `bridge/` 到 Windows 机器，修改 `bridge/config.json`：

- 项目名
- 串口号
- 日志目录
- 监听端口
- 烧录参数

然后执行 `npm start`。

## 第 3 步 安装技能

在模板仓库里运行：

```bash
bash skill/install_skill.sh
```

安装后把技能模板里的 `{PROJECT}`、`{PORT}`、`{BASE_URL}` 替换成你的项目值。

