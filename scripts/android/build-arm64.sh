#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../.." && pwd)"

# shellcheck source=env.sh
source "$script_dir/env.sh"

cd "$repo_root"
cmake --build --preset android-arm64-debug
