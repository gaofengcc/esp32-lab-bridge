#!/usr/bin/env bash
# 发布态组件静态检查：不需要 ESP-IDF，也不会编译代码。
#
# 依赖：优先用 ripgrep(rg)，没有则回退 grep（两者都没有直接失败退出）。
# 注意：不要用 `if rg ...; then fail; else pass; fi` 这种写法——rg 不存在时
# 命令返回 127 会走进 else 分支，导致"业务字符串扫描"假 PASS。
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

# --- 搜索工具探测：rg 优先，回退 grep，都没有则直接失败 ---
if command -v rg >/dev/null 2>&1; then
    SEARCH_TOOL=rg
elif command -v grep >/dev/null 2>&1; then
    SEARCH_TOOL=grep
else
    printf 'FAIL 缺少 rg 与 grep，无法执行检查\n'
    exit 1
fi
printf '搜索工具：%s\n\n' "$SEARCH_TOOL"

# 固定字符串匹配：has_fixed <文件> <字符串>
has_fixed() {
    if [[ "$SEARCH_TOOL" == "rg" ]]; then
        rg -qF "$2" "$1"
    else
        grep -qF "$2" "$1"
    fi
}

# 是否含中文字符：has_cjk <文件>
has_cjk() {
    if [[ "$SEARCH_TOOL" == "rg" ]]; then
        LC_ALL=C.UTF-8 rg -q '[一-龥]' "$1"
    else
        LC_ALL=C.UTF-8 grep -qP '[\x{4e00}-\x{9fa5}]' "$1"
    fi
}

# 业务字符串扫描：scan_forbidden
scan_forbidden() {
    if [[ "$SEARCH_TOOL" == "rg" ]]; then
        rg -n -i --glob '!*.o' --glob '!*.a' "$FORBIDDEN" "$ROOT/firmware" "$ROOT/wireless"
    else
        grep -rniE --exclude='*.o' --exclude='*.a' "$FORBIDDEN" "$ROOT/firmware" "$ROOT/wireless"
    fi
}

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
    if [[ -f "$metadata" ]] && has_fixed "$metadata" "$EXPECTED_VERSION"; then
        pass "版本号 $component -> 0.2.0"
    else
        fail "版本号 $component -> 0.2.0"
    fi

    chinese_header_ok=1
    while IFS= read -r header; do
        [[ -n "$header" ]] || continue
        if ! has_cjk "$header"; then
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
# 用命令替换捕获结果：既能判定，又不受"命令失败即 127 假 PASS"影响
forbidden_hits="$(scan_forbidden || true)"
if [[ -n "$forbidden_hits" ]]; then
    printf '%s\n' "$forbidden_hits" | head -20
    fail "组件目录存在禁止业务字符串"
else
    pass "组件目录无禁止业务字符串"
fi

printf '\n检查完成：%d 个失败项\n' "$FAILURES"
if [[ "$FAILURES" -ne 0 ]]; then
    exit 1
fi
exit 0
