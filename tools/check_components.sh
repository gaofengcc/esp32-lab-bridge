#!/usr/bin/env bash
# 发布态组件静态检查：不需要 ESP-IDF，也不会编译代码。
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPONENTS=(
    "firmware/cdc_command"
    "firmware/log_gate"
    "firmware/lvgl_screenshot"
    "wireless/diag_service"
    "wireless/ota_update"
)
EXPECTED_VERSION='version: "0.2.0"'
FORBIDDEN='nas_|nas-01|ext/|int/|mqtt|device_config|lcd35|LCD35'
FAILURES=0

pass() {
    printf 'PASS %s\n' "$1"
}

fail() {
    printf 'FAIL %s\n' "$1"
    FAILURES=$((FAILURES + 1))
}

check_file() {
    local path="$1"
    if [[ -f "$ROOT/$path" ]]; then
        pass "必备文件 $path"
    else
        fail "必备文件 $path"
    fi
}

printf '组件发布态静态检查（版本 0.2.0）\n'
printf '仓库：%s\n\n' "$ROOT"

for component in "${COMPONENTS[@]}"; do
    printf '[组件] %s\n' "$component"
    check_file "$component/CMakeLists.txt"
    check_file "$component/idf_component.yml"
    check_file "$component/README.md"

    header_count="$(find "$ROOT/$component/include" -maxdepth 1 -type f -name '*.h' 2>/dev/null | wc -l)"
    if [[ "$header_count" -gt 0 ]]; then
        pass "头文件目录 $component/include"
    else
        fail "头文件目录 $component/include"
    fi

    metadata="$ROOT/$component/idf_component.yml"
    if [[ -f "$metadata" ]] && rg -q --fixed-strings "$EXPECTED_VERSION" "$metadata"; then
        pass "版本号 $component -> 0.2.0"
    else
        fail "版本号 $component -> 0.2.0"
    fi

    chinese_header_ok=1
    while IFS= read -r header; do
        if ! LC_ALL=C.UTF-8 rg -q '[一-龥]' "$header"; then
            fail "中文注释 $header"
            chinese_header_ok=0
        fi
    done < <(find "$ROOT/$component/include" -maxdepth 1 -type f -name '*.h' 2>/dev/null | sort)
    if [[ "$chinese_header_ok" -eq 1 && "$header_count" -gt 0 ]]; then
        pass "中文注释 $component/include/*.h"
    fi
    printf '\n'
done

printf '[全仓库组件源码] 业务字符串扫描\n'
if rg -n -i --glob '!*.o' --glob '!*.a' "$FORBIDDEN" \
    "$ROOT/firmware" "$ROOT/wireless"; then
    fail "组件目录存在禁止业务字符串"
else
    pass "组件目录无禁止业务字符串"
fi

printf '\n检查完成：%d 个失败项\n' "$FAILURES"
if [[ "$FAILURES" -ne 0 ]]; then
    exit 1
fi
exit 0
