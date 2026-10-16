#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)

binary=${1:-"$repo_root/uplink/src/uplink.full"}
output_dir=${2:-"$repo_root/dist"}
version=${APPIMAGE_VERSION:-1.04}
linuxdeploy_url=${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage}
linuxdeploy_sha256=${LINUXDEPLOY_SHA256:-8aea8da0f7f7039d2a2cecb14657d752a222a5e1d3825caeef186c82f751cdd1}
appimage_runtime_url=${APPIMAGE_RUNTIME_URL:-https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64}
appimage_runtime_sha256=${APPIMAGE_RUNTIME_SHA256:-156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074}
case "$version" in
    ""|*[!A-Za-z0-9._-]*)
        echo "Invalid AppImage version: $version" >&2
        exit 1
        ;;
esac

if [ ! -x "$binary" ]; then
    echo "AppImage input executable is missing or not executable: $binary" >&2
    exit 1
fi

mkdir -p "$output_dir"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/uplinkos-appimage.XXXXXX")
cleanup() {
    rm -rf "$work_dir"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

appdir="$work_dir/UplinkOS-GOLD.AppDir"
linuxdeploy="$work_dir/linuxdeploy-x86_64.AppImage"
appimage_runtime="$work_dir/runtime-x86_64"
appimage="$output_dir/UplinkOS-GOLD-v${version}-x86_64.AppImage"

mkdir -p "$appdir/usr/bin"
install -m 755 "$binary" "$appdir/usr/bin/uplink"

# Uplink resolves these resources beside its executable. Copy every required
# archive as data, never as a symlink back into the source checkout.
for archive in \
    data.dat fonts.dat graphics.dat loading.dat music.dat \
    patch.dat patch2.dat patch3.dat sounds.dat world.dat
do
    source_file="$repo_root/uplink/bin/$archive"
    if [ ! -s "$source_file" ]; then
        echo "Required runtime archive is missing or empty: $source_file" >&2
        exit 1
    fi
    resolved_file=$(readlink -f -- "$source_file")
    case "$resolved_file" in
        "$repo_root"/*) ;;
        *)
            echo "Runtime archive resolves outside the repository: $source_file" >&2
            exit 1
            ;;
    esac
    install -m 644 "$source_file" "$appdir/usr/bin/$archive"
done

# These unpacked resources and the complete GOLD asset tree are also consumed
# directly at runtime.
for directory in data music uplinkHD
do
    source_dir="$repo_root/uplink/bin/$directory"
    if [ ! -d "$source_dir" ]; then
        echo "Required runtime directory is missing: $source_dir" >&2
        exit 1
    fi
    source_link=$(find "$source_dir" -type l -print -quit)
    if [ -n "$source_link" ]; then
        echo "Runtime directory contains a symlink: $source_link" >&2
        exit 1
    fi
    cp -a "$source_dir" "$appdir/usr/bin/$directory"
done

remaining_link=$(find "$appdir/usr/bin" -type l -print -quit)
if [ -n "$remaining_link" ]; then
    echo "AppImage runtime contains an external symlink: $remaining_link" >&2
    exit 1
fi

curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' \
    --output "$linuxdeploy" "$linuxdeploy_url"
printf '%s  %s\n' "$linuxdeploy_sha256" "$linuxdeploy" | sha256sum --check -
chmod 755 "$linuxdeploy"
curl --fail --location --retry 3 --proto '=https' --proto-redir '=https' \
    --output "$appimage_runtime" "$appimage_runtime_url"
printf '%s  %s\n' "$appimage_runtime_sha256" "$appimage_runtime" | sha256sum --check -
chmod 755 "$appimage_runtime"

rm -f "$appimage" "$appimage.sha256"
APPIMAGE_EXTRACT_AND_RUN=1 \
ARCH=x86_64 \
LDAI_NO_APPSTREAM=1 \
LDAI_RUNTIME_FILE="$appimage_runtime" \
LDAI_OUTPUT="$appimage" \
"$linuxdeploy" \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/uplink" \
    --desktop-file "$script_dir/uplinkos-gold.desktop" \
    --icon-file "$repo_root/Installer/data/uplink.png" \
    --output appimage

chmod 755 "$appimage"
(
    cd "$output_dir"
    sha256sum "$(basename "$appimage")" > "$(basename "$appimage").sha256"
)

printf 'Created %s\n' "$appimage"
