# OTA 更新组件

这是一个可被 ESP-IDF 新项目直接导入的 OTA 组件，提供 manifest 下载、
SHA256 校验、双分区写入、pending verify 和一次性回滚控制。

OTA 载荷可以是 **app-only bin**，也可以是 **merged bin**：组件会根据 URL
中的 `_merged.bin` 自动识别 merged 包，并跳过包内 `0x10000` 偏移后再写入
app 分区。SHA256 始终校验下载到的完整载荷。

## 3 步接入

1. 将 `wireless/ota_update` 作为组件目录加入工程，并在工程依赖中启用：

   ```cmake
   # 新项目根目录 CMakeLists.txt
   set(EXTRA_COMPONENT_DIRS
       "${CMAKE_CURRENT_LIST_DIR}/components/ota_update")
   ```

2. 在应用启动时初始化并执行启动自检：

   ```c
   #include "ota_update.h"

   ESP_ERROR_CHECK(ota_update_service_init());
   ESP_ERROR_CHECK(ota_update_boot_guard_init());
   ```

3. 收到更新指令后调用 URL 或 JSON 入口，成功后重启：

   ```c
   ESP_ERROR_CHECK(ota_update_start_from_manifest_auth(
       "https://example.invalid/manifest.json", "token"));
   esp_restart();
   ```

## API 清单

- `ota_update_service_init()`：初始化状态快照。
- `ota_update_boot_guard_init()`：检查 pending verify 并执行启动自检。
- `ota_update_mark_app_valid_after_selftest()`：自检通过后确认新固件。
- `ota_update_mark_app_invalid_and_reboot()`：标记无效并请求回滚重启。
- `ota_update_schedule_rollback_once()`：安排下一次启动执行一次回滚自检。
- `ota_update_get_status()`：读取运行分区、进度、错误和回滚状态。
- `ota_update_state_name()`：将状态枚举转换为稳定字符串。
- `ota_update_status_json()`：将状态编码为 JSON。
- `ota_update_start_from_manifest()`：从 manifest URL 执行无鉴权 OTA。
- `ota_update_start_from_manifest_auth()`：从 manifest URL 执行 Bearer 鉴权 OTA。
- `ota_update_start_from_manifest_json()`：直接使用 manifest JSON 执行 OTA。
- `ota_update_start_from_manifest_json_auth()`：直接使用 manifest JSON 执行鉴权 OTA。
- `ota_update_abort()`：清理当前状态。
- `ota_update_begin_upload()`、`ota_update_write_chunk()`、
  `ota_update_finish_upload()`：直接上传兼容接口，当前返回
  `ESP_ERR_NOT_SUPPORTED`。

## manifest 格式

组件兼容以下字段别名：

- 二进制地址：`url`、`bin_url`、`image_url`、`download_url`
- SHA256：`sha256`、`sha256_hex`、`digest_sha256`
- 大小：`size`、`length`、`image_size`、`content_length`

最小示例：

```json
{
  "url": "https://example.invalid/app.bin",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size": 123456
}
```

## Kconfig

- `CONFIG_OTA_UPDATE_MANIFEST_MAX_LEN`：manifest 最大缓冲区，默认 `4096`。
- `CONFIG_OTA_UPDATE_HTTP_TIMEOUT_MS`：HTTP 超时时间，默认 `15000` ms。
- `CONFIG_OTA_UPDATE_HTTP_READ_BUF_SIZE`：载荷读取缓冲区，默认 `4096` 字节。

## 已知坑

- merged 包必须包含 `_merged.bin` 文件名；否则组件会按 app-only bin 写入，
  可能把镜像头写入 app 分区。
- merged 包的 manifest `size` 和 SHA256 必须针对完整下载文件，而不是
  去掉 `0x10000` 前缀后的 app 内容。
- `ota_update_schedule_rollback_once()` 依赖应用已完成 NVS 初始化，
  且 NVS 分区必须可写。
- 必须使用双 app 分区并启用 ESP-IDF rollback 配置；单分区工程无法完成
  安全切换。
- 直接上传 API 是兼容占位，不会执行分片写入。
