# OTA 更新组件模板

这是一份可复用的 ESP-IDF OTA 组件模板，保留了无线调试最核心的更新链路：

- `manifest -> bin` 下载
- `SHA256` 校验
- 双分区写入
- rollback / pending verify 状态管理
- 直接上传接口占位，默认返回 `ESP_ERR_NOT_SUPPORTED`

## 典型状态流

`idle -> checking -> downloading -> writing -> verifying -> ready_to_reboot`

失败时进入 `failed`，不支持的直接上传接口进入 `unsupported`。

## manifest 格式

默认兼容这些字段名：

- 二进制地址：`url` / `bin_url` / `image_url` / `download_url`
- SHA256：`sha256` / `sha256_hex` / `digest_sha256`
- 大小：`size` / `length` / `image_size` / `content_length`

示例：

```json
{
  "url": "http://<server-ip>:8000/app.bin",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "size": 123456
}
```

## 运行时状态

`ota_update_status_t` 至少包含：

- `running_partition`
- `boot_partition`
- `update_partition`
- `pending_verify`
- `rollback_enabled`
- `progress`
- `downloaded_bytes`
- `expected_bytes`
- `last_error`

并额外保留了 `manifest_url` 和 `last_error_name` 作为通用调试字段。

## 兼容接口

以下接口保留，但当前模板未实现直接上传流程：

- `ota_update_begin_upload()`
- `ota_update_write_chunk()`
- `ota_update_finish_upload()`
- `ota_update_abort()`

默认行为是返回 `ESP_ERR_NOT_SUPPORTED`，便于上层在不同项目里逐步替换。
