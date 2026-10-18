#!/bin/bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$project_root"
if [[ "$(uname -s)" != Darwin ]]; then
  echo "This bootstrap installs Qt for macOS." >&2
  exit 1
fi
xcrun --find clang++ >/dev/null
python3 -m venv .tools/venv
.tools/venv/bin/python -m pip install aqtinstall==3.3.0 cmake==4.4.4 ninja==1.13.2
.tools/venv/bin/python -m aqt install-qt mac desktop 6.11.3 clang_64 \
  --outputdir .tools/Qt --archives qtbase qttools
echo "Dependencies installed. Run: bash scripts/qt/build-macos.sh --test --package --run"
