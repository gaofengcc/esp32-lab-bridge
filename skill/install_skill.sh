#!/usr/bin/env bash
set -euo pipefail

PROJECT="${PROJECT:-esp32-lab-bridge}"
PORT="${PORT:-COM5}"
BASE_URL="${BASE_URL:-http://127.0.0.1:3000}"
CODEX_HOME="${CODEX_HOME:-$HOME/.codex}"
TARGET_DIR="$CODEX_HOME/skills/${PROJECT}-win10-serial-lab-template"

mkdir -p "$TARGET_DIR"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SKILL_SOURCE="$SCRIPT_DIR/SKILL.md"
python3 - "$TARGET_DIR" "$PROJECT" "$PORT" "$BASE_URL" <<'PY'
from pathlib import Path
import os
import sys

target_dir = Path(sys.argv[1])
project = sys.argv[2]
port = sys.argv[3]
base_url = sys.argv[4]

source = Path(os.environ["SKILL_SOURCE"])
text = source.read_text(encoding="utf-8")
text = text.replace("{PROJECT}", project)
text = text.replace("{PORT}", port)
text = text.replace("{BASE_URL}", base_url)

(target_dir / "SKILL.md").write_text(text, encoding="utf-8")
print(f"已安装到: {target_dir}")
PY
