#!/usr/bin/env bash
# Build the KR-106 Schwung module (aarch64).
#
# Uses Docker for cross-compilation unless CROSS_PREFIX is set
# (e.g. CROSS_PREFIX=aarch64-linux-gnu- on an ARM host, or
#  CROSS_PREFIX="" for a native aarch64 build).
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="schwung-kr106-builder"

if [ -z "${CROSS_PREFIX+x}" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== KR-106 Module Build (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        echo "Building Docker image (first time only)..."
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi
    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        -e CROSS_PREFIX=aarch64-linux-gnu- \
        "$IMAGE_NAME" \
        ./scripts/build.sh
    echo "=== Done ==="
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX-aarch64-linux-gnu-}"

cd "$REPO_ROOT"

if [ ! -f src/dsp/kr106/KR106_DSP.h ]; then
    echo "DSP headers missing — run scripts/sync_dsp.sh first" >&2
    exit 1
fi

echo "=== Building KR-106 Module (cross prefix: '$CROSS_PREFIX') ==="
mkdir -p build dist/kr106

${CROSS_PREFIX}g++ -O3 -shared -fPIC -std=c++17 \
    -DNDEBUG -ffp-contract=fast -fno-math-errno \
    -mcpu=cortex-a72 \
    src/dsp/kr106_plugin.cpp \
    -o build/dsp.so \
    -Isrc/dsp \
    -lm

echo "Packaging..."
# regenerate the compiled-in UI hierarchy from module.json (source of truth)
python3 "$SCRIPT_DIR/gen_ui_hierarchy.py" 2>/dev/null || python "$SCRIPT_DIR/gen_ui_hierarchy.py"
cat src/module.json > dist/kr106/module.json
cat src/ui.js > dist/kr106/ui.js
cat build/dsp.so > dist/kr106/dsp.so
[ -f LICENSE ] && cat LICENSE > dist/kr106/LICENSE
chmod +x dist/kr106/dsp.so

cd dist
tar -czf kr106-module.tar.gz kr106/
cd ..

echo ""
echo "=== Build Complete ==="
echo "Output:  dist/kr106/"
echo "Tarball: dist/kr106-module.tar.gz"
