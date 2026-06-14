#!/usr/bin/env bash
set -euo pipefail

source_dir="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

desktop="${source_dir}/od-unix/share/applications/net.winuae.WinUAE.desktop"
mime="${source_dir}/od-unix/share/mime/packages/net.winuae.WinUAE.xml"
icon="${source_dir}/od-win32/resources/amiga.png"
cmake="${source_dir}/CMakeLists.txt"
readme="${source_dir}/README_unix.md"
deb_builder="${source_dir}/tools/debian-build-package.sh"
deb_postinst="${source_dir}/packaging/debian/postinst"
deb_postrm="${source_dir}/packaging/debian/postrm"

require_file() {
    local path="$1"
    local label="$2"
    if [[ ! -f "${path}" ]]; then
        echo "error: missing ${label}: ${path}" >&2
        exit 1
    fi
}

require_match() {
    local pattern="$1"
    local path="$2"
    local label="$3"
    if ! grep -Eq "${pattern}" "${path}"; then
        echo "error: ${label} not found in ${path}" >&2
        exit 1
    fi
}

require_file "${desktop}" "desktop entry"
require_file "${mime}" "MIME package"
require_file "${icon}" "Linux icon source"
require_file "${cmake}" "CMake package rules"
require_file "${readme}" "README package documentation"
require_file "${deb_builder}" "Debian package builder"
require_file "${deb_postinst}" "Debian postinst control script"
require_file "${deb_postrm}" "Debian postrm control script"

require_match '^\[Desktop Entry\]$' "${desktop}" "desktop header"
require_match '^Type=Application$' "${desktop}" "desktop application type"
require_match '^Name=WinUAE$' "${desktop}" "desktop name"
require_match '^Exec=winuae %f$' "${desktop}" "desktop exec"
require_match '^Icon=winuae$' "${desktop}" "desktop icon"
require_match '^Categories=.*(^|;)Emulator(;|$)' "${desktop}" "desktop category"
require_match '^MimeType=application/x-winuae-config;$' "${desktop}" "desktop MIME type"

require_match '<mime-type type="application/x-winuae-config">' "${mime}" "MIME type"
require_match '<glob pattern="\*\.uae"/>' "${mime}" "MIME glob"

if ! file "${icon}" | grep -q 'PNG image data'; then
    echo "error: Linux icon source is not a PNG: ${icon}" >&2
    exit 1
fi

require_match 'install\(PROGRAMS "\$<TARGET_FILE:winuae_unix>"' "${cmake}" "Linux executable install rule"
require_match 'RENAME winuae' "${cmake}" "Linux executable install name"
require_match 'WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE' "${cmake}" "Linux plugin install directory"
require_match 'WINUAE_QEMU_UAE_PLUGIN_FILE' "${cmake}" "prebuilt QEMU-UAE plugin option"
require_match 'winuae_unix_qemu_uae_package_check' "${cmake}" "QEMU-UAE package requirement"
require_match 'install\(FILES "\$\{WINUAE_QEMU_UAE_PLUGIN_PATH\}"' "${cmake}" "QEMU-UAE plugin install rule"
require_match 'od-unix/share/applications/net\.winuae\.WinUAE\.desktop' "${cmake}" "desktop install rule"
require_match 'od-unix/share/mime/packages/net\.winuae\.WinUAE\.xml' "${cmake}" "MIME install rule"
require_match 'icons/hicolor/256x256/apps' "${cmake}" "application icon install rule"
require_match 'icons/hicolor/256x256/mimetypes' "${cmake}" "MIME icon install rule"
require_match 'set\(CPACK_GENERATOR "TGZ"\)' "${cmake}" "CPack TGZ generator"
require_match 'list\(APPEND CPACK_GENERATOR "DEB"\)' "${cmake}" "CPack DEB generator"
require_match 'CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON' "${cmake}" "CPack Debian shlibdeps"
require_match 'CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA' "${cmake}" "CPack Debian control scripts"
require_match 'packaging/debian/postinst' "${cmake}" "Debian postinst registration"
require_match 'packaging/debian/postrm' "${cmake}" "Debian postrm registration"
require_match 'add_dependencies\(package winuae_unix_qemu_uae_plugin\)' "${cmake}" "package plugin dependency"

require_match 'cmake --install /tmp/winuae_cmake_build --prefix /opt/winuae' "${readme}" "Linux install instructions"
require_match 'cmake --build /tmp/winuae_cmake_build --target package' "${readme}" "Linux package instructions"
require_match 'tools/debian-build-package.sh' "${readme}" "Debian package helper instructions"
require_match 'installs the emulator as `winuae`' "${readme}" "Debian command name documentation"
require_match 'qemu-uae\.so' "${readme}" "QEMU-UAE package documentation"
require_match 'WINUAE_UNIX_WITH_PPC_QEMU:BOOL' "${deb_builder}" "Debian plugin requirement"
require_match '/usr/bin/winuae' "${deb_builder}" "Debian installed binary check"
require_match 'package does not contain qemu-uae\.so' "${deb_builder}" "Debian plugin contents check"

echo "verified Linux package metadata"
