#!/usr/bin/env bash
set -euo pipefail

PROJECT="${PROJECT:-esp32-lab-bridge}"
PORT="${PORT:-COM5}"
BASE_URL="${BASE_URL:-http://127.0.0.1:3000}"
CODEX_HOME="${CODEX_HOME:-$HOME/.codex}"
TARGET_DIR="${TARGET_DIR:-$CODEX_HOME/skills/${PROJECT}-win10-serial-lab}"

mkdir -p "$TARGET_DIR"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SKILL_DIR="$SCRIPT_DIR"
python3 - "$TARGET_DIR" "$PROJECT" "$PORT" "$BASE_URL" <<'PY'
from pathlib import Path
import os
import shutil
import sys

target_dir = Path(sys.argv[1])
project = sys.argv[2]
port = sys.argv[3]
base_url = sys.argv[4]

skill_dir = Path(os.environ["SKILL_DIR"])

replacements = {
    "{PROJECT}": project,
    "{PORT}": port,
    "{BASE_URL}": base_url,
}


def render_text(text: str) -> str:
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def render_file(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    try:
        text = source.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        shutil.copy2(source, destination)
        return
    destination.write_text(render_text(text), encoding="utf-8")

render_file(skill_dir / "SKILL.md", target_dir / "SKILL.md")

source_refs = skill_dir / "references"
target_refs = target_dir / "references"
if source_refs.exists():
    if target_refs.exists():
        shutil.rmtree(target_refs)
    for source in sorted(path for path in source_refs.rglob("*") if path.is_file()):
        render_file(source, target_refs / source.relative_to(source_refs))

print(f"已安装到: {target_dir}")
PY
