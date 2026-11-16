#!/usr/bin/env bash
# 在自己的电脑上编译固件，产出 dist/island-full.bin。
# 需要 git，以及二选一：已安装并 export 过的 ESP-IDF 5.5.x，或者 Docker。
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$HERE/.build"
mkdir -p "$WORK" "$HERE/dist"

if [ ! -d "$WORK/ai-passport/.git" ]; then
    git clone --depth 1 https://github.com/FoloToy/ai-passport.git "$WORK/ai-passport"
fi
find "$WORK/ai-passport/main" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' -o -name 'CMakeLists.txt' \) -delete
cp "$HERE"/main/* "$WORK/ai-passport/main/"

CMD='idf.py set-target esp32c3 && idf.py build && idf.py merge-bin -o island-full.bin'
if command -v idf.py >/dev/null 2>&1; then
    (cd "$WORK/ai-passport" && bash -c "$CMD")
elif command -v docker >/dev/null 2>&1; then
    docker run --rm -v "$WORK/ai-passport:/project" -w /project espressif/idf:v5.5.3 bash -c "$CMD"
else
    echo "没有找到 idf.py，也没有 Docker。请先安装其中之一。" >&2
    exit 1
fi

cp "$WORK/ai-passport/build/island-full.bin" "$HERE/dist/"
echo
echo "固件已生成：$HERE/dist/island-full.bin"
echo "烧录（会清掉设备上原有的存档）："
echo "  python -m esptool --chip esp32c3 -p <串口> write_flash 0x0 dist/island-full.bin"
