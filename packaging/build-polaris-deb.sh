#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
DIST_DIR="${ROOT_DIR}/dist"
PACKAGE_NAME="polaris-studio"
ARCH="$(dpkg --print-architecture)"
VERSION="${POLARIS_VERSION:-0.1.7+git$(git -C "${ROOT_DIR}" rev-parse --short HEAD)}"
STAGE_DIR="$(mktemp -d "${ROOT_DIR}/.polaris-deb-stage.XXXXXXXX")"

cleanup() { rm -rf "${STAGE_DIR}"; }
trap cleanup EXIT

if [ "${ARCH}" != "amd64" ]; then
    echo "This package script currently targets amd64; detected ${ARCH}." >&2
    exit 1
fi
if [ ! -x "${BUILD_DIR}/polaris-studio" ] || [ ! -x "${BUILD_DIR}/polaris-engine" ]; then
    echo "polaris-studio and polaris-engine must be built before packaging." >&2
    exit 1
fi
if [ ! -x "${BUILD_DIR}/stable-diffusion.cpp/bin/sd-cli" ]; then
    echo "Bundled stable-diffusion.cpp sd-cli is missing; rebuild with POLARIS_BUNDLED_SD=ON." >&2
    exit 1
fi

echo "[deb] staging ${PACKAGE_NAME} ${VERSION} (${ARCH})"
mkdir -p \
    "${STAGE_DIR}/DEBIAN" \
    "${STAGE_DIR}/usr/bin" \
    "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}" \
    "${STAGE_DIR}/usr/share/applications" \
    "${STAGE_DIR}/usr/share/icons/hicolor/scalable/apps" \
    "${STAGE_DIR}/usr/share/icons/hicolor/64x64/apps" \
    "${STAGE_DIR}/usr/share/metainfo" \
    "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}"

# binaries
install -m 0755 "${BUILD_DIR}/polaris-studio" "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/polaris-studio"
install -m 0755 "${BUILD_DIR}/polaris-engine" "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/polaris-engine"
install -m 0755 "${BUILD_DIR}/stable-diffusion.cpp/bin/sd-cli" \
    "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/sd-cli"

# polaris-engine links against the local GGML backend shared libraries. The
# executable has an $ORIGIN runpath, so place these beside it in the package.
for runtime_lib in "${BUILD_DIR}"/libggml*.so*; do
    [ -e "${runtime_lib}" ] || continue
    install -m 0755 "${runtime_lib}" "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/"
done

# Keep any shared libraries produced by the isolated stable-diffusion.cpp
# build beside sd-cli so the installed desktop package is self-contained.
for runtime_lib in "${BUILD_DIR}/stable-diffusion.cpp/bin/"*.so*; do
    [ -e "${runtime_lib}" ] || continue
    install -m 0755 "${runtime_lib}" "${STAGE_DIR}/usr/lib/${PACKAGE_NAME}/"
done

# symlink in PATH
ln -sf "../lib/${PACKAGE_NAME}/polaris-studio" "${STAGE_DIR}/usr/bin/polaris-studio"

# desktop integration
install -m 0644 "${ROOT_DIR}/packaging/polaris-studio.desktop" \
    "${STAGE_DIR}/usr/share/applications/polaris-studio.desktop"
install -m 0644 "${ROOT_DIR}/packaging/com.polaris.studio.metainfo.xml" \
    "${STAGE_DIR}/usr/share/metainfo/com.polaris.studio.metainfo.xml"
install -m 0644 "${ROOT_DIR}/README.md" "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/README.md"
if [ -f "${ROOT_DIR}/vendor/stable-diffusion.cpp/LICENSE" ]; then
    install -m 0644 "${ROOT_DIR}/vendor/stable-diffusion.cpp/LICENSE" \
        "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/stable-diffusion.cpp.LICENSE"
fi

# icon (reuse existing SVG, resize for 64x64)
if [ -f "${ROOT_DIR}/assets/acestep.svg" ]; then
    install -m 0644 "${ROOT_DIR}/assets/acestep.svg" \
        "${STAGE_DIR}/usr/share/icons/hicolor/scalable/apps/polaris-studio.svg"
    if command -v convert >/dev/null 2>&1; then
        convert -background none "${ROOT_DIR}/assets/acestep.svg" -resize 64x64 \
            "${STAGE_DIR}/usr/share/icons/hicolor/64x64/apps/polaris-studio.png"
    fi
fi

cat >"${STAGE_DIR}/DEBIAN/control" <<EOF
Package: ${PACKAGE_NAME}
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: ${ARCH}
Maintainer: cpntodd <acestep.cpp@localhost>
Depends: libqt6core6t64 | libqt6core6, libqt6gui6, libqt6widgets6,
         libqt6qml6, libqt6quick6, libqt6quickcontrols2-6, libqt6network6,
         libqt6sql6, libqt6sql6-sqlite, libqt6multimedia6,
         libvulkan1, libc6, libgcc-s1, libstdc++6, libgomp1,
         qml6-module-qtquick-controls, qml6-module-qtquick-layouts,
         qml6-module-qtquick-dialogs, qml6-module-qtmultimedia
Recommends: mesa-vulkan-drivers | nvidia-vulkan-icd
Description: Polaris Studio — local AI music and pixel-art workstation
 A native Qt 6 desktop application for private, offline AI music and image
 generation powered by GGML. Describe a song or create a detailed pixel-art
 asset in Pixel Lab — no cloud, no subscription, no browser. The bundled
 stable-diffusion.cpp runtime is installed with the desktop application;
 model weights are downloaded in-app from Hugging Face on first run.
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
dpkg-deb --build --root-owner-group "${STAGE_DIR}" "${OUTPUT}"
echo "[deb] complete: ${OUTPUT}"
du -h "${OUTPUT}"
