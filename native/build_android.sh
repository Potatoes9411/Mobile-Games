#!/usr/bin/env bash
# ===========================================================================
# Pocket Arcade — Android APK build script
# Cross-compiles the C engine for arm64-v8a, packages with aapt2, signs with
# a debug keystore. No Gradle, no Java code, no Android Studio.
# ===========================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR"
SRC="$ROOT/src"
ANDROID_DIR="$ROOT/android"
DIST="$ROOT/../dist"

# SDK paths — override with env vars if installed elsewhere
ANDROID_SDK="${ANDROID_SDK:-/root/.cache/android/sdk}"
NDK_ROOT="${NDK_ROOT:-$ANDROID_SDK/ndk/android-ndk-r28c}"
BUILD_TOOLS="${BUILD_TOOLS:-$ANDROID_SDK/build-tools/android-15}"
PLATFORM_JAR="${PLATFORM_JAR:-$ANDROID_SDK/platforms/android-35/android.jar}"

NDK_TOOLCHAIN="$NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64"
GLUE_SRC="$NDK_ROOT/sources/android/native_app_glue"

# Target: arm64-v8a (API 21 minimum)
CC_ARM64="$NDK_TOOLCHAIN/bin/aarch64-linux-android26-clang"
# Target: armeabi-v7a
CC_ARM32="$NDK_TOOLCHAIN/bin/armv7a-linux-androideabi26-clang"

AAPT2="$BUILD_TOOLS/aapt2"
D8="$BUILD_TOOLS/d8"
ZIPALIGN="$BUILD_TOOLS/zipalign"
APKSIGNER="$BUILD_TOOLS/apksigner"

# Temporary build directory
BUILD="$ROOT/build_android"
rm -rf "$BUILD"
mkdir -p "$BUILD"

# Keystore for debug signing
KEYSTORE="${KEYSTORE:-$BUILD/debug.keystore}"
KEYSTORE_PASS="${KEYSTORE_PASS:-android}"
KEY_ALIAS="${KEY_ALIAS:-androiddebugkey}"

# Source files (everything except platform_win32.c)
SOURCES=(
    "$SRC/raster.c"
    "$SRC/font.c"
    "$SRC/audio.c"
    "$SRC/save.c"
    "$SRC/hub.c"
    "$SRC/platform_android.c"
)
for f in "$SRC"/games/*.c; do SOURCES+=("$f"); done

CFLAGS="-O2 -Wall -DANDROID -D__ANDROID__ -I$SRC -I$GLUE_SRC -fPIC"
LDFLAGS="-shared -landroid -laaudio -llog -lm"

# --------------------------------------------------------- compile --------
compile_arch() {
    local ARCH="$1"
    local CC="$2"
    local ABI_DIR="$BUILD/lib/$ARCH"
    local OBJ_DIR="$BUILD/obj/$ARCH"
    mkdir -p "$ABI_DIR" "$OBJ_DIR"

    echo "[$ARCH] Compiling..."

    # Compile each source file
    for src in "${SOURCES[@]}"; do
        local base=$(basename "$src" .c)
        "$CC" $CFLAGS -c "$src" -o "$OBJ_DIR/${base}.o"
    done

    # Compile native_app_glue
    "$CC" $CFLAGS -c "$GLUE_SRC/android_native_app_glue.c" -o "$OBJ_DIR/glue.o"

    # Link shared library
    "$CC" $LDFLAGS "$OBJ_DIR"/*.o -o "$ABI_DIR/libpocketarcade.so"

    echo "[$ARCH] Built libpocketarcade.so ($(du -h "$ABI_DIR/libpocketarcade.so" | cut -f1))"
}

echo "=== Building arm64-v8a ==="
compile_arch "arm64-v8a" "$CC_ARM64"

echo "=== Building armeabi-v7a ==="
compile_arch "armeabi-v7a" "$CC_ARM32"

# ---------------------------------------------------- package APK ---------
echo "=== Packaging APK ==="

# Compile manifest resources
"$AAPT2" link \
    --manifest "$ANDROID_DIR/AndroidManifest.xml" \
    -I "$PLATFORM_JAR" \
    -o "$BUILD/base.apk" \
    --min-sdk-version 26 \
    --target-sdk-version 35 \
    -v

# The base.apk from aapt2 link is a valid zip; we add our .so files to it.
# Use a temp directory to build the final APK structure.
APK_STAGE="$BUILD/apk_stage"
mkdir -p "$APK_STAGE"

# Extract the aapt2 output
cd "$APK_STAGE"
unzip -q -o "$BUILD/base.apk"

# Add native libraries
mkdir -p "lib/arm64-v8a" "lib/armeabi-v7a"
cp "$BUILD/lib/arm64-v8a/libpocketarcade.so" "lib/arm64-v8a/"
cp "$BUILD/lib/armeabi-v7a/libpocketarcade.so" "lib/armeabi-v7a/"

# Rebuild the APK zip — .so and resources.arsc MUST be STORED (uncompressed)
# or Android refuses to install the APK.
cd "$BUILD"
rm -f unsigned.apk
(cd "$APK_STAGE" && \
    zip -q -0 "$BUILD/unsigned.apk" resources.arsc && \
    zip -q -0 -r "$BUILD/unsigned.apk" lib/ && \
    zip -q -r "$BUILD/unsigned.apk" AndroidManifest.xml)

# Zipalign
"$ZIPALIGN" -f 4 "$BUILD/unsigned.apk" "$BUILD/aligned.apk"

# ------------------------------------------------- debug signing ----------
echo "=== Signing APK ==="

# Generate debug keystore if not provided
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair \
        -keystore "$KEYSTORE" \
        -storepass "$KEYSTORE_PASS" \
        -keypass "$KEYSTORE_PASS" \
        -alias "$KEY_ALIAS" \
        -keyalg RSA \
        -keysize 2048 \
        -validity 10000 \
        -dname "CN=Debug,O=PocketArcade,C=US" \
        2>/dev/null
    echo "Generated debug keystore"
fi

# Sign with apksigner
"$APKSIGNER" sign \
    --ks "$KEYSTORE" \
    --ks-pass "pass:$KEYSTORE_PASS" \
    --ks-key-alias "$KEY_ALIAS" \
    --key-pass "pass:$KEYSTORE_PASS" \
    --out "$BUILD/PocketArcade.apk" \
    "$BUILD/aligned.apk"

# Verify
"$APKSIGNER" verify "$BUILD/PocketArcade.apk" && echo "APK signature valid"

# Copy to dist
mkdir -p "$DIST"
cp "$BUILD/PocketArcade.apk" "$DIST/PocketArcade.apk"

echo ""
echo "========================================="
echo " APK built: dist/PocketArcade.apk"
echo " Size: $(du -h "$DIST/PocketArcade.apk" | cut -f1)"
echo "========================================="
