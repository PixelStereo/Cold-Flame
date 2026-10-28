#!/usr/bin/env bash
# Install macOS dependencies, build Cold Flame, and launch the application.
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$project_dir/build-macos"
launch_app=1
install_dependencies=1
app_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-run) launch_app=0; shift ;;
        --no-deps) install_dependencies=0; shift ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo 'Missing path after --build-dir' >&2; exit 2; }
            build_dir="$2"; shift 2 ;;
        --help|-h)
            cat <<'HELP'
Usage: ./build.sh [--no-run] [--no-deps] [--build-dir PATH] [-- APP_ARGS...]

Installs Homebrew if needed, then Qt 6, CMake, Ninja and zlib.
Builds a Release macOS application and opens it.

Examples:
  ./build.sh
  ./build.sh --no-run
  ./build.sh -- "$PWD/examples/theatre.cfl"
HELP
            exit 0 ;;
        --) shift; app_args=("$@"); break ;;
        *) echo "Unknown option: $1 (use --help)" >&2; exit 2 ;;
    esac
done
[[ "$(uname -s)" == Darwin ]] || { echo 'This script targets macOS. See README.md for other systems.' >&2; exit 1; }

brew_bin="$(command -v brew || true)"
if [[ -z "$brew_bin" ]]; then
    for candidate in /opt/homebrew/bin/brew /usr/local/bin/brew; do
        if [[ -x "$candidate" ]]; then brew_bin="$candidate"; break; fi
    done
fi
if [[ -z "$brew_bin" ]]; then
    [[ "$install_dependencies" == 1 ]] || { echo 'Homebrew is required; rerun without --no-deps.' >&2; exit 1; }
    echo 'Installing Homebrew using its official installer (may request your macOS password).'
    bootstrap="$(mktemp -t cold-flame-homebrew)"
    trap 'rm -f "$bootstrap"' EXIT
    curl --fail --silent --show-error --location https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh -o "$bootstrap"
    /bin/bash "$bootstrap"
    rm -f "$bootstrap"
    trap - EXIT
    if [[ -x /opt/homebrew/bin/brew ]]; then brew_bin=/opt/homebrew/bin/brew; else brew_bin=/usr/local/bin/brew; fi
fi
if [[ "$install_dependencies" == 1 ]]; then
    missing=()
    for package in cmake ninja qt@6 zlib; do
        if ! "$brew_bin" list --versions "$package" >/dev/null 2>&1; then missing+=("$package"); fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then "$brew_bin" install "${missing[@]}"; fi
fi
brew_prefix="$("$brew_bin" --prefix)"
export PATH="$brew_prefix/bin:$PATH"
qt_prefix="$("$brew_bin" --prefix qt@6)"
zlib_prefix="$("$brew_bin" --prefix zlib)"
cmake_bin="$("$brew_bin" --prefix cmake)/bin/cmake"
parallel_jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-$(sysctl -n hw.ncpu)}"
case "$parallel_jobs" in ''|*[!0-9]*|0) parallel_jobs=2 ;; esac
if [[ "$parallel_jobs" -gt 8 ]]; then parallel_jobs=8; fi

"$cmake_bin" -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt_prefix" "-DZLIB_ROOT=$zlib_prefix"
"$cmake_bin" --build "$build_dir" --parallel "$parallel_jobs"
"$cmake_bin" --install "$build_dir" --prefix "$build_dir/install"
if [[ "$launch_app" == 1 ]]; then
    app_path="$build_dir/cold-flame.app"
    if [[ ! -d "$app_path" ]]; then echo "Application bundle not found: $app_path" >&2; exit 1; fi
    if [[ ${#app_args[@]} -gt 0 ]]; then open -n "$app_path" --args "${app_args[@]}"; else open -n "$app_path"; fi
fi
