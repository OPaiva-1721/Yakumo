#!/usr/bin/env bash
# Packs an APK of Ninja Impact for arm64-v8a without Gradle, from a build
# configured with the NDK toolchain and -DMHP3RD_ANDROID_APP=ON. It carries
# no game data: the player's disc image is installed on the device.
# scripts/release_android.sh uses it for releases; on its own it makes test
# builds.
#
#   build_apk.sh <build dir> <SDL3 source dir> <libSDL3.so> <output.apk> [overlay limit]
#
# FONT_DIR may name a directory holding NotoSansCJKjp-Regular.otf and
# NotoSansCJK-LICENSE.txt, the fallback font and licence a Linux release
# fetches (packaging/linux/sources.sh pins both); they go into the APK's
# assets, and the app unpacks the font on its first start. Without it the
# game's text is blank unless the device has a font stb_truetype reads.
#
# ANDROID_HOME must name the SDK (build-tools and a platform are needed).
# DEBUGGABLE=1 marks the app debuggable, so `adb shell run-as` reaches its
# files (its log, its data) on a test device; leave it out for players.
# DEVICE_TEST_PACKAGE=<unique.name.devicetest> generates an isolated test APK
# contract. Requires DEBUGGABLE=1 and MHP3RD_RELEASE=OFF in the native build.
# It repacks existing libraries; it does not rebuild AOT or overlays.
# DEVICE_TEST_PERFORMANCE=settings keeps the performance menu editable; the
# default log mode forces diagnostic logging. Match the runner's performance field.
# KEYSTORE (with KEYSTORE_PASS and KEY_ALIAS) signs with a key of your own, so
# that later builds install over earlier ones and keep the player's data;
# without it a throwaway key is made in the build directory.
#
# The app needs Android 10 (API 29), where every 64-bit device has Vulkan
# 1.1: build the native code for android-29 (ANDROID_PLATFORM), or it may
# import libc functions Android 10 lacks (libc++ waits with
# pthread_cond_clockwait from android-30 on). Newer Android features are
# looked up at run time.
# The overlay limit packs only the first N overlay libraries, to keep a test
# APK small; without it every one is packed.
set -euo pipefail

build_dir="$(cd "$1" && pwd)"
sdl_dir="$(cd "$2" && pwd)"
sdl_lib="$3"
output="$4"
overlay_limit="${5:-}"
here="$(cd "$(dirname "$0")" && pwd)"

sdk="${ANDROID_HOME:?set ANDROID_HOME to the Android SDK}"
build_tools="$(ls -d "$sdk"/build-tools/* | sort -V | tail -1)"

# Build tools as the SDK ships them: d8 and apksigner are batch files on
# Windows, which Git Bash cannot run directly.
run_tool() {
    local name="$1"
    shift
    if [[ -f "$build_tools/$name.bat" ]]; then
        local args=() arg
        for arg in "$@"; do
            if [[ "$arg" == /* ]]; then args+=("$(cygpath -w "$arg")"); else args+=("$arg"); fi
        done
        cmd.exe //c "$(cygpath -w "$build_tools/$name.bat")" "${args[@]}"
    else
        "$build_tools/$name" "$@"
    fi
}

# Adds files to a zip archive: `zip` where there is one, else Python's
# zipfile (Git Bash on Windows has no zip). Paths are relative to $1.
add_to_zip() {
    local dir="$1" archive="$2"
    shift 2
    if command -v zip > /dev/null; then
        (cd "$dir" && zip -q -r -9 "$archive" "$@")
    else
        (cd "$dir" && python3 -c '
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "a", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for item in sys.argv[2:]:
        paths = [item] if os.path.isfile(item) else [os.path.join(r, f) for r, _, fs in os.walk(item) for f in fs]
        for p in sorted(paths):
            z.write(p, p.replace(os.sep, "/"))
' "$archive" "$@")
    fi
}
platform="$(ls -d "$sdk"/platforms/android-* | sort -V | tail -1)"
android_jar="$platform/android.jar"
work="$build_dir/apk"
rm -rf "$work"
mkdir -p "$work/classes" "$work/dex" "$work/lib/arm64-v8a"

echo "compiling SDL's Java activity and the app's own"
javac -nowarn -encoding UTF-8 --release 11 -classpath "$android_jar" -d "$work/classes" \
    $(find "$sdl_dir/android-project/app/src/main/java" "$here/java" -name '*.java') 2> "$work/javac.log"
min_sdk=29
(cd "$work/classes" && run_tool d8 --release --min-api "$min_sdk" --lib "$android_jar" --output ../dex \
    $(find . -name '*.class'))

echo "linking resources"
# The licences of what the APK carries: the third-party notices, SDL's and
# FFmpeg's (with where its source is), and the font's when it is packed.
mkdir -p "$work/assets/licenses"
cp "$here/../THIRD_PARTY_NOTICES.md" "$work/assets/licenses/"
cp "$sdl_dir/LICENSE.txt" "$work/assets/licenses/SDL3-LICENSE.txt"
# A build configured with MHP3RD_FFMPEG=OFF carries no FFmpeg.
if [[ -f "$build_dir/bin/lib/libavcodec.so" ]]; then
    cp "$build_dir"/bin/lib/FFmpeg-COPYING.LGPLv2.1.txt "$build_dir"/bin/lib/FFmpeg-SOURCE.txt "$work/assets/licenses/"
fi
# GAME_FILES_DIR: a personal build only. It packs the player's own EBOOT.ELF
# and keys.txt from that directory, so setup asks for the disc image alone.
# Such an APK holds the player's game executable and console keys: never
# share or publish it.
if [[ -n "${GAME_FILES_DIR:-}" ]]; then
    mkdir -p "$work/assets/game"
    cp "$GAME_FILES_DIR/EBOOT.ELF" "$GAME_FILES_DIR/keys.txt" "$work/assets/game/"
fi
if [[ -n "${FONT_DIR:-}" ]]; then
    mkdir -p "$work/assets/fonts"
    cp "$FONT_DIR/NotoSansCJKjp-Regular.otf" "$work/assets/fonts/"
    cp "$FONT_DIR/NotoSansCJK-LICENSE.txt" "$work/assets/licenses/NotoSansCJK-OFL.txt"
fi
manifest="$here/AndroidManifest.xml"
if [[ -n "${DEVICE_TEST_PACKAGE:-}" ]]; then
    [[ "${DEBUGGABLE:-}" == 1 ]] || { echo "device tests require DEBUGGABLE=1" >&2; exit 1; }
    grep -q '^MHP3RD_RELEASE:BOOL=OFF$' "$build_dir/CMakeCache.txt" || {
        echo "device-test APKs require a developer native build" >&2; exit 1;
    }
    manifest="$work/device-testing-manifest.xml"
    observation_args=()
    if [[ "${DEVICE_TEST_OBSERVATIONS:-0}" == 1 ]]; then observation_args=(--observations); fi
    python3 "$here/../../tools/device_testing.py" android-manifest \
        --package "$DEVICE_TEST_PACKAGE" --output "$manifest" \
        --performance "${DEVICE_TEST_PERFORMANCE:-log}" \
        "${observation_args[@]}" \
        --contract "$work/assets/device-testing.json"
fi
assets=(-A "$work/assets")
"$build_tools/aapt2" compile --dir "$here/res" -o "$work/res.zip"
# The version: `git describe` of the checkout, and the number of commits as
# the code Android compares, so a later build is always an update
# (VERSION_NAME and VERSION_CODE override them).
repo="$(cd "$here/../../../.." && pwd)"
version_name="${VERSION_NAME:-$(git -C "$repo" describe --tags --always --dirty 2>/dev/null || echo 0.0)}"
version_code="${VERSION_CODE:-$(git -C "$repo" rev-list --count HEAD 2>/dev/null || echo 1)}"
"$build_tools/aapt2" link -I "$android_jar" --manifest "$manifest" \
    --min-sdk-version "$min_sdk" --target-sdk-version 35 --version-name "$version_name" \
    --version-code "$version_code" ${DEBUGGABLE:+--debug-mode} "${assets[@]}" -o "$work/unsigned.apk" \
    "$work/res.zip"

echo "adding native libraries"
lib="$work/lib/arm64-v8a"
cp "$sdl_lib" "$lib/libSDL3.so"
cp "$build_dir/bin/libmain.so" "$lib/"
if [[ -f "$build_dir/bin/lib/libavcodec.so" ]]; then
    cp "$build_dir"/bin/lib/libavcodec.so "$build_dir"/bin/lib/libavutil.so "$lib/"
fi
# A title without code overlays has none to pack.
shopt -s nullglob
overlays=("$build_dir"/bin/overlays/libovl*.so)
shopt -u nullglob
if [[ -n "$overlay_limit" ]]; then overlays=("${overlays[@]:0:$overlay_limit}"); fi
if ((${#overlays[@]} > 0)); then cp "${overlays[@]}" "$lib/"; fi
strip="$(ls -d "$ANDROID_NDK"/toolchains/llvm/prebuilt/*/bin/llvm-strip* | head -1)"
"$strip" --strip-unneeded "$lib"/*.so
# extractNativeLibs is on, so the libraries may be compressed: the package
# manager unpacks them at install time.
cp "$work/dex/classes.dex" "$work/"
add_to_zip "$work" unsigned.apk classes.dex lib

echo "aligning and signing"
keystore="${KEYSTORE:-$build_dir/apk-research.keystore}"
password="${KEYSTORE_PASS:-research}"
alias="${KEY_ALIAS:-research}"
if [[ ! -f "$keystore" ]]; then
    [[ -n "${KEYSTORE:-}" ]] && { echo "error: $keystore not found" >&2; exit 1; }
    keytool -genkeypair -keystore "$keystore" -storepass research -keypass research -alias research \
        -keyalg RSA -keysize 2048 -validity 3650 -dname "CN=Yakumo research build" > /dev/null 2>&1
fi
"$build_tools/zipalign" -f -P 16 4 "$work/unsigned.apk" "$work/aligned.apk"
run_tool apksigner sign --ks "$keystore" --ks-pass "pass:$password" --key-pass "pass:$password" \
    --ks-key-alias "$alias" --out "$output" "$work/aligned.apk"
echo "wrote $output ($(du -h "$output" | cut -f1), ${#overlays[@]} overlay libraries, version $version_name ($version_code))"
