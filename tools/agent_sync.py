#!/usr/bin/env python3
"""项目级 Agent 上下文同步工具。

这个脚本只依赖 Python 标准库，用于让 Cursor、Codex 或其他 agent
在同一个仓库内读写共享上下文。
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import textwrap
import time
from typing import Any


SYNC_DIR = ".agent-sync"
EVENTS_FILE = "events.jsonl"
CURRENT_FILE = "current.md"
LOCK_FILE = ".lock"
LOCK_TTL_SECONDS = 300
DEFAULT_LAST = 20
MAX_BODY_IN_CURRENT = 1200
RUNTIME_DIRS = ("inbox", "imports", "sessions", "state", "private", "attachments")

KINDS = (
    "note",
    "handoff",
    "decision",
    "todo",
    "question",
    "result",
    "import",
)
STATUSES = ("info", "open", "done", "blocked")


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )


def compact_time(value: str) -> str:
    return re.sub(r"[^0-9]", "", value)[:14]


def sanitize_agent(value: str) -> str:
    value = value.strip().lower()
    value = re.sub(r"[^a-z0-9_.-]+", "-", value)
    return value.strip("-") or "agent"


def trim(value: str, limit: int) -> str:
    value = value.strip()
    if len(value) <= limit:
        return value
    return value[: limit - 3].rstrip() + "..."


def find_project_root(start: Path) -> Path:
    current = start.resolve()
    for candidate in (current, *current.parents):
        if (candidate / ".git").exists():
            return candidate
    return current


def sync_dir(root: Path) -> Path:
    return root / SYNC_DIR


def events_path(root: Path) -> Path:
    return sync_dir(root) / EVENTS_FILE


def current_path(root: Path) -> Path:
    return sync_dir(root) / CURRENT_FILE


def ensure_layout(root: Path) -> None:
    sync_dir(root).mkdir(parents=True, exist_ok=True)
    for name in RUNTIME_DIRS:
        (sync_dir(root) / name).mkdir(parents=True, exist_ok=True)


class FileLock:
    def __init__(self, root: Path) -> None:
        self.path = sync_dir(root) / LOCK_FILE
        self.fd: int | None = None

    def __enter__(self) -> "FileLock":
        deadline = time.time() + 10
        while True:
            try:
                self.fd = os.open(
                    self.path,
                    os.O_CREAT | os.O_EXCL | os.O_WRONLY,
                    0o644,
                )
                payload = f"pid={os.getpid()} created_at={utc_now()}\n"
                os.write(self.fd, payload.encode("utf-8"))
                return self
            except FileExistsError:
                if self._is_stale():
                    try:
                        self.path.unlink()
                        continue
                    except FileNotFoundError:
                        continue
                if time.time() >= deadline:
                    raise TimeoutError(f"同步锁等待超时: {self.path}")
                time.sleep(0.1)

    def __exit__(self, exc_type: object, exc: object, tb: object) -> None:
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
        try:
            self.path.unlink()
        except FileNotFoundError:
            pass

    def _is_stale(self) -> bool:
        try:
            stat = self.path.stat()
        except FileNotFoundError:
            return False
        return (time.time() - stat.st_mtime) > LOCK_TTL_SECONDS


def parse_csv(values: list[str] | None) -> list[str]:
    if not values:
        return []
    result: list[str] = []
    for value in values:
        for item in value.split(","):
            item = item.strip()
            if item:
                result.append(item)
    return result


def read_text_input(body: str | None, body_file: str | None, use_stdin: bool) -> str:
    pieces: list[str] = []
    if body:
        pieces.append(body)
    if body_file:
        pieces.append(Path(body_file).read_text(encoding="utf-8"))
    if use_stdin:
        pieces.append(sys.stdin.read())
    return "\n\n".join(piece.strip() for piece in pieces if piece.strip())


def make_event(
    *,
    agent: str,
    kind: str,
    summary: str,
    body: str,
    status: str,
    scope: str,
    files: list[str],
    tags: list[str],
    source: str,
) -> dict[str, Any]:
    created_at = utc_now()
    agent = sanitize_agent(agent)
    digest = hashlib.sha1(
        f"{created_at}\n{agent}\n{kind}\n{summary}\n{body}".encode("utf-8")
    ).hexdigest()[:10]
    event_id = f"{compact_time(created_at)}-{agent}-{digest}"
    return {
        "id": event_id,
        "created_at": created_at,
        "agent": agent,
        "kind": kind,
        "status": status,
        "scope": scope,
        "summary": summary.strip(),
        "body": body.strip(),
        "files": files,
        "tags": tags,
        "source": source.strip(),
    }


def read_events(root: Path) -> list[dict[str, Any]]:
    path = events_path(root)
    if not path.exists():
        return []
    events: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_no} JSONL 解析失败: {exc}") from exc
            if isinstance(event, dict):
                events.append(event)
    return events


def append_event(root: Path, event: dict[str, Any]) -> None:
    path = events_path(root)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(event, ensure_ascii=False, sort_keys=True))
        handle.write("\n")


def atomic_write(path: Path, content: str) -> None:
    with tempfile.NamedTemporaryFile(
        "w",
        encoding="utf-8",
        delete=False,
        dir=str(path.parent),
        prefix=f".{path.name}.",
        suffix=".tmp",
    ) as handle:
        tmp_path = Path(handle.name)
        handle.write(content)
    tmp_path.replace(path)


def event_line(event: dict[str, Any], include_body: bool = True) -> str:
    files = event.get("files") or []
    tags = event.get("tags") or []
    line = (
        f"- `{event.get('created_at', '')}` "
        f"`{event.get('agent', 'agent')}/{event.get('kind', 'note')}/"
        f"{event.get('status', 'info')}` {event.get('summary', '').strip()}"
    )
    details: list[str] = []
    if files:
        details.append("文件: " + ", ".join(f"`{item}`" for item in files))
    if tags:
        details.append("标签: " + ", ".join(f"`{item}`" for item in tags))
    if event.get("source"):
        details.append(f"来源: `{event.get('source')}`")
    body = str(event.get("body") or "").strip()
    if include_body and body:
        details.append("内容: " + trim(body.replace("\n", " / "), MAX_BODY_IN_CURRENT))
    if details:
        line += "\n  " + "\n  ".join(details)
    return line


def render_current(root: Path, events: list[dict[str, Any]], last: int) -> str:
    now = utc_now()
    recent = events[-last:]
    open_items = [
        event
        for event in events
        if event.get("status") in {"open", "blocked"}
        or event.get("kind") in {"todo", "question"}
        and event.get("status") != "done"
    ]
    decisions = [
        event
        for event in events
        if event.get("kind") == "decision" and event.get("status") != "blocked"
    ][-10:]

    lines = [
        "# Agent Sync Current",
        "",
        "> 自动生成文件。Cursor、Codex 和其他 agent 启动任务时优先读取本文件；",
        "> 需要追加上下文时请使用 `python tools/agent_sync.py record ...`。",
        "",
        f"- 项目根目录: `{root}`",
        f"- 更新时间: `{now}`",
        f"- 事件总数: `{len(events)}`",
        f"- 事件日志: `{SYNC_DIR}/{EVENTS_FILE}`",
        "",
        "## 快速用法",
        "",
        "```bash",
        "python tools/agent_sync.py status",
        "python tools/agent_sync.py record --agent codex --kind handoff --summary \"完成了某项修改\" --body \"关键细节\"",
        "python tools/agent_sync.py append --agent cursor --message \"补充一条 Cursor 侧上下文\"",
        "python tools/agent_sync.py record --agent cursor --kind decision --summary \"确认某个实现方向\" --file source/idf/example.c",
        "python tools/agent_sync.py import --agent cursor --file .agent-sync/inbox/cursor-chat.txt --summary \"导入 Cursor 对话摘要\"",
        "python tools/agent_sync.py merge --agent cursor",
        "```",
        "",
        "## 打开事项",
        "",
    ]

    if open_items:
        lines.extend(event_line(event, include_body=True) for event in open_items[-20:])
    else:
        lines.append("- 暂无打开事项。")

    lines.extend(["", "## 最近决策", ""])
    if decisions:
        lines.extend(event_line(event, include_body=True) for event in decisions)
    else:
        lines.append("- 暂无决策记录。")

    lines.extend(["", "## 最近记录", ""])
    if recent:
        lines.extend(event_line(event, include_body=True) for event in recent)
    else:
        lines.append("- 暂无记录。")

    lines.extend(
        [
            "",
            "## 协作约定",
            "",
            "- 每个 agent 开始工作前先读本文件，必要时再查看 `events.jsonl` 的末尾。",
            "- 重要结论、未完成事项、风险和交接信息写入事件日志。",
            "- 不在同步区记录 WiFi 密码、token、私钥、真实账号等敏感信息。",
            "- 长对话先总结再写入，原始 transcript 只在确有必要时放入 `.agent-sync/inbox/`，再用 `merge` 合并。",
            "",
        ]
    )
    return "\n".join(lines)


def rebuild_current(root: Path, last: int) -> None:
    events = read_events(root)
    atomic_write(current_path(root), render_current(root, events, last))


def default_summary_from_file(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    for line in text.splitlines():
        line = line.strip()
        if line:
            return trim(line, 120)
    return f"导入 {path.name}"


def cmd_init(args: argparse.Namespace) -> int:
    root = find_project_root(Path(args.root or "."))
    ensure_layout(root)
    with FileLock(root):
        events = read_events(root)
        if not events and args.seed:
            event = make_event(
                agent=args.agent,
                kind="note",
                summary="初始化项目级 agent 同步区",
                body="建立 Cursor/Codex 共享上下文入口。",
                status="info",
                scope="project",
                files=[f"{SYNC_DIR}/{CURRENT_FILE}", f"{SYNC_DIR}/{EVENTS_FILE}"],
                tags=["agent-sync", "init"],
                source="agent_sync.py init",
            )
            append_event(root, event)
        rebuild_current(root, args.last)
    print(f"同步区已就绪: {sync_dir(root)}")
    print(f"当前摘要: {current_path(root)}")
    return 0


def cmd_record(args: argparse.Namespace) -> int:
    root = find_project_root(Path(args.root or "."))
    ensure_layout(root)
    body = read_text_input(args.body, args.body_file, args.stdin)
    event = make_event(
        agent=args.agent,
        kind=args.kind,
        summary=args.summary,
        body=body,
        status=args.status,
        scope=args.scope,
        files=parse_csv(args.file),
        tags=parse_csv(args.tag),
        source=args.source,
    )
    with FileLock(root):
        append_event(root, event)
        rebuild_current(root, args.last)
    print(f"已记录事件: {event['id']}")
    print(f"当前摘要: {current_path(root)}")
    return 0


def cmd_import(args: argparse.Namespace) -> int:
    source_path = Path(args.file)
    if not source_path.exists():
        raise FileNotFoundError(source_path)
    root = find_project_root(Path(args.root or "."))
    ensure_layout(root)
    body = source_path.read_text(encoding="utf-8")
    summary = args.summary or default_summary_from_file(source_path)
    event = make_event(
        agent=args.agent,
        kind="import",
        summary=summary,
        body=trim(body, args.max_chars),
        status=args.status,
        scope=args.scope,
        files=parse_csv(args.related_file),
        tags=parse_csv(args.tag) or ["import"],
        source=args.source or str(source_path),
    )
    with FileLock(root):
        append_event(root, event)
        rebuild_current(root, args.last)
    print(f"已导入对话: {event['id']}")
    print(f"当前摘要: {current_path(root)}")
    return 0


def cmd_merge(args: argparse.Namespace) -> int:
    root = find_project_root(Path(args.root or "."))
    ensure_layout(root)
    inbox = sync_dir(root) / args.inbox
    if not inbox.exists():
        raise FileNotFoundError(inbox)

    files = [
        item
        for item in sorted(inbox.iterdir())
        if item.is_file()
        and item.suffix.lower() in {".txt", ".md", ".json", ".jsonl"}
        and not item.name.startswith(".")
    ]
    if not files:
        print(f"没有待导入文件: {inbox}")
        return 0

    if args.dry_run:
        print(f"待导入文件目录: {inbox}")
        for item in files:
            print(f"- {item}")
        return 0

    imported: list[str] = []
    with FileLock(root):
        for item in files:
            body = item.read_text(encoding="utf-8")
            event = make_event(
                agent=args.agent,
                kind="import",
                summary=args.summary or default_summary_from_file(item),
                body=trim(body, args.max_chars),
                status=args.status,
                scope=args.scope,
                files=[],
                tags=parse_csv(args.tag) or ["inbox", "import"],
                source=str(item),
            )
            append_event(root, event)
            imported.append(event["id"])
            if args.archive:
                archive_dir = sync_dir(root) / "sessions"
                archive_dir.mkdir(parents=True, exist_ok=True)
                target = archive_dir / f"{event['id']}-{item.name}"
                item.replace(target)
        rebuild_current(root, args.last)

    print(f"已导入 {len(imported)} 个文件。")
    for event_id in imported:
        print(f"- {event_id}")
    print(f"当前摘要: {current_path(root)}")
    return 0


def cmd_append(args: argparse.Namespace) -> int:
    args.kind = args.kind or "note"
    args.status = args.status or "info"
    args.scope = args.scope or "project"
    args.summary = args.summary or trim(args.message, 80)
    args.body = args.body or args.message
    args.body_file = None
    args.stdin = False
    args.file = args.file
    args.tag = args.tag
    args.source = args.source or "agent_sync.py append"
    return cmd_record(args)


def check_ignored(root: Path, relative_path: str) -> bool:
    import subprocess

    result = subprocess.run(
        ["git", "check-ignore", "-q", relative_path],
        cwd=root,
        check=False,
    )
    return result.returncode == 0


def cmd_doctor(args: argparse.Namespace) -> int:
    root = find_project_root(Path(args.root or "."))
    ensure_layout(root)
    events = read_events(root)
    checks: list[tuple[str, bool, str]] = []

    checks.append(("同步目录存在", sync_dir(root).is_dir(), str(sync_dir(root))))
    checks.append(("事件日志可读", True, str(events_path(root))))
    checks.append(("当前摘要存在", current_path(root).exists(), str(current_path(root))))
    for name in RUNTIME_DIRS:
        rel = f"{SYNC_DIR}/{name}/test.txt"
        checks.append((f"{name}/ 已被 git 忽略", check_ignored(root, rel), rel))
    checks.append(
        (
            f"{EVENTS_FILE} 已被 git 忽略",
            check_ignored(root, f"{SYNC_DIR}/{EVENTS_FILE}"),
            f"{SYNC_DIR}/{EVENTS_FILE}",
        )
    )
    checks.append(
        (
            f"{CURRENT_FILE} 已被 git 忽略",
            check_ignored(root, f"{SYNC_DIR}/{CURRENT_FILE}"),
            f"{SYNC_DIR}/{CURRENT_FILE}",
        )
    )

    sensitive_patterns = re.compile(
        r"(password|passwd|token|secret|private[_-]?key|api[_-]?key)",
        re.IGNORECASE,
    )
    suspicious: list[str] = []
    for event in events:
        haystack = "\n".join(
            str(event.get(key, "")) for key in ("summary", "body", "source")
        )
        if sensitive_patterns.search(haystack):
            suspicious.append(str(event.get("id", "<unknown>")))

    all_ok = all(ok for _, ok, _ in checks) and not suspicious
    for name, ok, detail in checks:
        mark = "OK" if ok else "FAIL"
        print(f"[{mark}] {name}: {detail}")
    if suspicious:
        print("[WARN] 事件中出现疑似敏感关键词，请人工复核:")
        for event_id in suspicious:
            print(f"- {event_id}")
    print(f"事件总数: {len(events)}")
    return 0 if all_ok else 1


def cmd_status(args: argparse.Namespace) -> int:
    root = find_project_root(Path(args.root or "."))
    ensure_layout(root)
    events = read_events(root)
    if args.rebuild:
        with FileLock(root):
            rebuild_current(root, args.last)
    if args.json:
        print(
            json.dumps(
                {
                    "root": str(root),
                    "sync_dir": str(sync_dir(root)),
                    "current": str(current_path(root)),
                    "events": str(events_path(root)),
                    "event_count": len(events),
                    "last": events[-args.last :],
                },
                ensure_ascii=False,
                indent=2,
            )
        )
        return 0

    print(f"项目根目录: {root}")
    print(f"同步目录: {sync_dir(root)}")
    print(f"当前摘要: {current_path(root)}")
    print(f"事件日志: {events_path(root)}")
    print(f"事件总数: {len(events)}")
    print("")
    print(f"最近 {min(args.last, len(events))} 条:")
    for event in events[-args.last :]:
        print(event_line(event, include_body=False))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Cursor/Codex 项目级上下文同步工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent(
            """\
            示例:
              python tools/agent_sync.py init
              python tools/agent_sync.py record --agent codex --kind handoff --summary "完成同步脚本"
              python tools/agent_sync.py record --agent cursor --kind todo --status open --summary "补充 MQTT_SET 测试"
              python tools/agent_sync.py import --agent cursor --file .agent-sync/inbox/cursor-chat.txt
            """
        ),
    )
    parser.add_argument("--root", help="项目根目录，默认从当前目录向上查找 .git")
    subparsers = parser.add_subparsers(dest="command", required=True)

    init_parser = subparsers.add_parser("init", help="创建同步目录并刷新 current.md")
    init_parser.add_argument("--agent", default="codex", help="初始化事件来源")
    init_parser.add_argument("--last", type=int, default=DEFAULT_LAST, help="current.md 保留最近记录数")
    init_parser.add_argument(
        "--no-seed",
        dest="seed",
        action="store_false",
        help="空日志时不写入初始化事件",
    )
    init_parser.set_defaults(func=cmd_init)

    record_parser = subparsers.add_parser("record", help="追加一条同步事件")
    record_parser.add_argument("--agent", default=os.environ.get("AGENT_NAME", "agent"))
    record_parser.add_argument("--kind", choices=KINDS, default="note")
    record_parser.add_argument("--status", choices=STATUSES, default="info")
    record_parser.add_argument("--scope", default="project")
    record_parser.add_argument("--summary", required=True)
    record_parser.add_argument("--body")
    record_parser.add_argument("--body-file")
    record_parser.add_argument("--stdin", action="store_true", help="从标准输入读取 body")
    record_parser.add_argument("--file", action="append", help="相关文件，可重复或用逗号分隔")
    record_parser.add_argument("--tag", action="append", help="标签，可重复或用逗号分隔")
    record_parser.add_argument("--source", default="agent_sync.py record")
    record_parser.add_argument("--last", type=int, default=DEFAULT_LAST)
    record_parser.set_defaults(func=cmd_record)

    import_parser = subparsers.add_parser("import", help="导入一份已保存的对话文本")
    import_parser.add_argument("--agent", default=os.environ.get("AGENT_NAME", "agent"))
    import_parser.add_argument("--file", required=True, help="要导入的文本文件")
    import_parser.add_argument("--summary")
    import_parser.add_argument("--status", choices=STATUSES, default="info")
    import_parser.add_argument("--scope", default="project")
    import_parser.add_argument("--related-file", action="append", help="相关项目文件")
    import_parser.add_argument("--tag", action="append")
    import_parser.add_argument("--source", default="")
    import_parser.add_argument("--max-chars", type=int, default=4000)
    import_parser.add_argument("--last", type=int, default=DEFAULT_LAST)
    import_parser.set_defaults(func=cmd_import)

    merge_parser = subparsers.add_parser("merge", help="导入 inbox 中的文本文件")
    merge_parser.add_argument("--agent", default=os.environ.get("AGENT_NAME", "agent"))
    merge_parser.add_argument("--inbox", default="inbox")
    merge_parser.add_argument("--summary")
    merge_parser.add_argument("--status", choices=STATUSES, default="info")
    merge_parser.add_argument("--scope", default="project")
    merge_parser.add_argument("--tag", action="append")
    merge_parser.add_argument("--max-chars", type=int, default=4000)
    merge_parser.add_argument("--last", type=int, default=DEFAULT_LAST)
    merge_parser.add_argument("--archive", action="store_true", help="导入后移入 sessions/")
    merge_parser.add_argument("--dry-run", action="store_true")
    merge_parser.set_defaults(func=cmd_merge)

    append_parser = subparsers.add_parser("append", help="record 的快捷别名")
    append_parser.add_argument("--agent", default=os.environ.get("AGENT_NAME", "agent"))
    append_parser.add_argument("--message", required=True)
    append_parser.add_argument("--summary")
    append_parser.add_argument("--body")
    append_parser.add_argument("--kind", choices=KINDS, default="note")
    append_parser.add_argument("--status", choices=STATUSES, default="info")
    append_parser.add_argument("--scope", default="project")
    append_parser.add_argument("--file", action="append")
    append_parser.add_argument("--tag", action="append")
    append_parser.add_argument("--source", default="agent_sync.py append")
    append_parser.add_argument("--last", type=int, default=DEFAULT_LAST)
    append_parser.set_defaults(func=cmd_append)

    status_parser = subparsers.add_parser("status", help="查看同步状态")
    status_parser.add_argument("--last", type=int, default=10)
    status_parser.add_argument("--json", action="store_true")
    status_parser.add_argument("--rebuild", action="store_true", help="重新生成 current.md")
    status_parser.set_defaults(func=cmd_status)

    doctor_parser = subparsers.add_parser("doctor", help="检查同步区配置是否健康")
    doctor_parser.set_defaults(func=cmd_doctor)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except Exception as exc:
        print(f"agent_sync 失败: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
