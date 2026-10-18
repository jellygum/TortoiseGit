#!/bin/bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$project_root"
export PATH="$project_root/.tools/venv/bin:$PATH"
qt_root="${QT_ROOT:-$project_root/.tools/Qt/6.11.3/macos}"
build_directory="${TGIT_BUILD_DIR:-$project_root/build-qt}"
package_directory="${TGIT_DIST_DIR:-$project_root/dist-qt}"
run_tests=false
make_package=false
run_app=false
for option in "$@"; do
  case "$option" in
    --test) run_tests=true ;;
    --package) make_package=true ;;
    --run) run_app=true ;;
    *) echo "Usage: $0 [--test] [--package] [--run]" >&2; exit 2 ;;
  esac
done
if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null || [[ ! -d "$qt_root" ]]; then
  echo "Install dependencies with bash scripts/qt/bootstrap-macos.sh or set QT_ROOT and PATH." >&2
  exit 1
fi
cmake -S "$project_root" -B "$build_directory" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" \
  -DCMAKE_PREFIX_PATH="$qt_root" -DCMAKE_BUILD_TYPE="${TGIT_BUILD_TYPE:-Release}" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.4
cmake --build "$build_directory" --parallel "${TGIT_BUILD_JOBS:-4}"
if $run_tests; then
  ctest --test-dir "$build_directory" --output-on-failure
fi
app_path="$build_directory/TortoiseGitQt.app"
if $make_package; then
  cmake --install "$build_directory" --prefix "$package_directory"
  app_path="$package_directory/TortoiseGitQt.app"
  python3 "$project_root/scripts/qt/verify-bundle.py" "$app_path"
  echo "Local development bundle: $app_path"
fi
if $run_app; then
  open "$app_path" --args --repository "$project_root"
fi
