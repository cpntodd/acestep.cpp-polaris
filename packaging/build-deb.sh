#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
MODEL_DIR="${ROOT_DIR}/models"
DIST_DIR="${ROOT_DIR}/dist"
PACKAGE_NAME="acestep-cpp"
ARCH="$(dpkg --print-architecture)"
VERSION="${ACE_STEP_VERSION:-0.1.0+git$(git -C "${ROOT_DIR}" rev-parse --short HEAD)}"
DEB_COMPRESSION="${ACE_DEB_COMPRESSION:-xz}"
DEB_COMPRESSION_LEVEL="${ACE_DEB_COMPRESSION_LEVEL:-9}"
# xz is single-threaded unless requested otherwise. The full model bundle is
# large, so use all available cores by default while still allowing callers to
# provide their own XZ_OPT policy.
export XZ_OPT="${XZ_OPT:--threads=0}"
# The full model bundle is larger than the default 7.8 GiB tmpfs on some
# desktop systems. Stage beside the checkout on the same disk as the output.
STAGE_DIR="$(mktemp -d "${ROOT_DIR}/.deb-stage.XXXXXXXX")"
DEB_TMP_DIR="$(mktemp -d "${ROOT_DIR}/.deb-work.XXXXXXXX")"

cleanup() {
	rm -rf "${STAGE_DIR}"
	rm -rf "${DEB_TMP_DIR}"
}
trap cleanup EXIT
export TMPDIR="${DEB_TMP_DIR}"

if [ "${ARCH}" != "amd64" ]; then
    echo "This package script currently targets amd64; detected ${ARCH}." >&2
    exit 1
fi
if [ ! -d "${MODEL_DIR}" ] || ! find "${MODEL_DIR}" -type f -print -quit | grep -q .; then
    echo "No model files found in ${MODEL_DIR}; refusing to build an empty runtime." >&2
    exit 1
fi
if [ ! -x "${BUILD_DIR}/ace-server" ] || [ ! -x "${BUILD_DIR}/acestep-supervisor" ]; then
    echo "The server and supervisor must be built before packaging." >&2
    exit 1
fi
if ! command -v dpkg-deb >/dev/null 2>&1 || ! command -v convert >/dev/null 2>&1; then
    echo "dpkg-deb and ImageMagick's convert are required to build the branded package." >&2
    exit 1
fi

echo "[deb] staging ${PACKAGE_NAME} ${VERSION} (${ARCH})"
mkdir -p \
    "${STAGE_DIR}/DEBIAN" \
    "${STAGE_DIR}/usr/bin" \
    "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/models" \
    "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/adapters" \
    "${STAGE_DIR}/usr/share/applications" \
    "${STAGE_DIR}/usr/share/icons/hicolor/scalable/apps" \
    "${STAGE_DIR}/usr/share/icons/hicolor/16x16/apps" \
    "${STAGE_DIR}/usr/share/icons/hicolor/24x24/apps" \
    "${STAGE_DIR}/usr/share/icons/hicolor/32x32/apps" \
    "${STAGE_DIR}/usr/share/icons/hicolor/48x48/apps" \
    "${STAGE_DIR}/usr/share/icons/hicolor/64x64/apps" \
    "${STAGE_DIR}/usr/share/icons/hicolor/128x128/apps" \
    "${STAGE_DIR}/usr/share/icons/hicolor/256x256/apps" \
    "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}"

for tool in ace-server acestep-supervisor ace-lm ace-synth ace-understand neural-codec mp3-codec quantize; do
    if [ -x "${BUILD_DIR}/${tool}" ]; then
        install -m 0755 "${BUILD_DIR}/${tool}" "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/${tool}"
    else
        echo "[deb] warning: ${tool} was not built; omitting it" >&2
    fi
done

cp -a "${BUILD_DIR}"/libggml*.so* "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/"
cp -a "${MODEL_DIR}"/. "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/models/"
cp -a "${ROOT_DIR}/adapters"/. "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/adapters/" 2>/dev/null || true

install -m 0755 "${ROOT_DIR}/packaging/acestep-cpp-launcher" "${STAGE_DIR}/usr/bin/acestep-cpp"
install -m 0755 "${ROOT_DIR}/packaging/acestep-cpp-stop" "${STAGE_DIR}/usr/bin/acestep-cpp-stop"
install -m 0644 "${ROOT_DIR}/packaging/acestep-cpp.desktop" "${STAGE_DIR}/usr/share/applications/acestep-cpp.desktop"
install -m 0644 "${ROOT_DIR}/assets/acestep.svg" "${STAGE_DIR}/usr/share/icons/hicolor/scalable/apps/acestep-cpp.svg"
install -m 0644 "${ROOT_DIR}/README.md" "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/README.md"
install -m 0644 "${ROOT_DIR}/LICENSE" "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/LICENSE"
install -m 0644 "${ROOT_DIR}/vendor/whisper/LICENSE" "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/WHISPER-LICENSE"

for size in 16 24 32 48 64 128 256; do
    convert -background none "${ROOT_DIR}/assets/acestep.svg" -resize "${size}x${size}" \
        "${STAGE_DIR}/usr/share/icons/hicolor/${size}x${size}/apps/acestep-cpp.png"
done

cat >"${STAGE_DIR}/DEBIAN/control" <<EOF
Package: ${PACKAGE_NAME}
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: ${ARCH}
Maintainer: cpntodd <acestep.cpp@localhost>
Depends: bash, curl, xdg-utils, libvulkan1, libc6, libgcc-s1, libstdc++6, libgomp1
Recommends: mesa-vulkan-drivers | nvidia-vulkan-icd | intel-media-va-driver
Description: ACE-Step.cpp local AI music workstation
 A branded desktop launcher for cpntodd/acestep.cpp-polaris with the embedded WebUI,
 local reference workflow, English/Macedonian speech-language listener (CPU by
 default, with optional local Vulkan mode),
 production tools, and all models included in the build checkout. The server
 runs on localhost and opens the private UI.
EOF

cat >"${STAGE_DIR}/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "${STAGE_DIR}/DEBIAN/postinst"

mkdir -p "${DIST_DIR}"
OUTPUT="${DIST_DIR}/${PACKAGE_NAME}_${VERSION}_${ARCH}.deb"
rm -f "${OUTPUT}"
echo "[deb] building ${OUTPUT}"
dpkg-deb --build --root-owner-group --compression="${DEB_COMPRESSION}" --compression-level="${DEB_COMPRESSION_LEVEL}" "${STAGE_DIR}" "${OUTPUT}"
echo "[deb] complete: ${OUTPUT}"
du -h "${OUTPUT}"
