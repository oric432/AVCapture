#!/usr/bin/env bash
set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THIRD_PARTY_DIR="${SCRIPT_DIR}/third_party"
XORG_RELEASES_URL="${XORG_RELEASES_URL:-https://www.x.org/releases/individual}"
XCB_RELEASES_URL="${XCB_RELEASES_URL:-https://xcb.freedesktop.org/dist}"
DOWNLOAD_DIR="${SCRIPT_DIR}/_downloads"
INSTALL_PREFIX="/usr/local"
BUILD_DIR="${SCRIPT_DIR}/_build"
JOBS=$(nproc)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${GREEN}[BUILD]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

# Print banner
print_banner() {
    echo "=================================================="
    echo "  ScreenRecorder Dependencies Build Script"
    echo "  (Online upstream source downloads)"
    echo "=================================================="
    echo "Third-party dir: ${THIRD_PARTY_DIR}"
    echo "X.Org releases URL: ${XORG_RELEASES_URL}"
    echo "XCB releases URL: ${XCB_RELEASES_URL}"
    echo "Download cache: ${DOWNLOAD_DIR}"
    echo "Install prefix: ${INSTALL_PREFIX}"
    echo "Build directory: ${BUILD_DIR}"
    echo "CPU cores: ${JOBS}"
    echo "=================================================="
    echo
}

# Check for required tools
check_dependencies() {
    log "Checking for required build tools..."
    local missing=()
    
    for tool in gcc g++ make pkg-config tar curl; do
        if ! command -v $tool &> /dev/null; then
            missing+=($tool)
        fi
    done
    
    if [ ${#missing[@]} -ne 0 ]; then
        error "Missing required tools: ${missing[*]}\nInstall with: sudo apt-get install build-essential pkg-config curl"
    fi
    
    log "All required tools found!"
}

# Verify source directories exist
verify_sources() {
    log "Verifying source directories..."
    
    if [ ! -d "${THIRD_PARTY_DIR}/x264" ]; then
        error "x264 source not found at: ${THIRD_PARTY_DIR}/x264"
    fi
    
    if [ ! -d "${THIRD_PARTY_DIR}/FFmpeg" ]; then
        error "FFmpeg source not found at: ${THIRD_PARTY_DIR}/FFmpeg"
    fi
    
    log "All sources found!"
}

# Create directory structure
setup_dirs() {
    log "Setting up directory structure..."
    mkdir -p "${BUILD_DIR}"
    mkdir -p "${INSTALL_PREFIX}"
    mkdir -p "${DOWNLOAD_DIR}"
}

# Download file from an upstream URL
download_file() {
    local url=$1
    local filename="${url##*/}"
    local dest_path="${DOWNLOAD_DIR}/${filename}"
    
    # Check if already downloaded
    if [ -f "${dest_path}" ]; then
        info "Using cached ${filename}"
        return 0
    fi
    
    info "Downloading ${filename}..."
    
    if curl -f -L --connect-timeout 10 --retry 3 -o "${dest_path}.tmp" "${url}"; then
        mv "${dest_path}.tmp" "${dest_path}"
        log "Downloaded ${filename}"
    else
        rm -f "${dest_path}.tmp"
        error "Failed to download ${filename} from ${url}"
    fi
}

# Build x264
build_x264() {
    log "Building x264..."
    
    local X264_BUILD="${BUILD_DIR}/x264"
    
    info "Copying x264 source..."
    rm -rf "${X264_BUILD}"
    cp -r "${THIRD_PARTY_DIR}/x264" "${X264_BUILD}"
    
    cd "${X264_BUILD}"
    
    info "Configuring x264..."
    ./configure \
        --prefix="${INSTALL_PREFIX}" \
        --enable-static \
        --disable-opencl \
        --disable-cli
    
    info "Building x264..."
    make -j"${JOBS}"
    
    info "Installing x264..."
    make install
    
    info "Cleaning x264 build..."
    make clean
    
    log "x264 built successfully!"
}

# Build Zlib
build_zlib() {
    log "Building zlib..."

    local ZLIB_BUILD="${BUILD_DIR}/zlib"

    info "Copying zlib source..."
    rm -rf "${ZLIB_BUILD}"
    cp -r "${THIRD_PARTY_DIR}/zlib" "${ZLIB_BUILD}"

    cd "${ZLIB_BUILD}"

    info "Configuring zlib..."
    ./configure --static

    info "Installing zlib..."
    make install
    
    info "Cleaning zlib build..."
    make clean
    
    log "zlib built successfully!"
}   


# Build FFmpeg
build_ffmpeg() {
    log "Building FFmpeg..."
    
    local FFMPEG_BUILD="${BUILD_DIR}/FFmpeg"
    
    info "Copying FFmpeg source..."
    rm -rf "${FFMPEG_BUILD}"
    cp -r "${THIRD_PARTY_DIR}/FFmpeg" "${FFMPEG_BUILD}"
    
    cd "${FFMPEG_BUILD}"
    
    export PKG_CONFIG_PATH="${INSTALL_PREFIX}/lib/pkgconfig/x264.pc:${PKG_CONFIG_PATH}"
    
    info "Configuring FFmpeg..."
    
    ./configure \
        --prefix="${INSTALL_PREFIX}" \
        --pkg-config-flags="--static" \
        --enable-static \
        --disable-shared \
        --disable-debug \
        --disable-doc \
        --disable-programs \
        --disable-everything \
        --enable-gpl \
        --enable-libx264 \
        --enable-encoder=libx264 \
        --enable-encoder=h264_nvenc \
        --enable-encoder=h264_qsv \
        --enable-encoder=h264_vaapi \
        --enable-encoder=aac \
        --enable-encoder=pcm_s16le \
        --enable-decoder=aac \
        --enable-decoder=pcm_s16le \
        --enable-muxer=mp4 \
        --enable-muxer=wav \
        --enable-muxer=mpegts \
        --enable-demuxer=wav \
        --enable-demuxer=aac \
        --enable-demuxer=concat \
        --enable-demuxer=mpegts \
        --enable-parser=aac \
        --enable-parser=h264 \
        --enable-protocol=file \
        --enable-bsf=aac_adtstoasc \
        --enable-bsf=h264_mp4toannexb \
        --enable-avcodec \
        --enable-avformat \
        --enable-avutil \
        --enable-swscale \
        --enable-swresample \
        --extra-cflags="-I${INSTALL_PREFIX}/include" \
        --extra-ldflags="-L${INSTALL_PREFIX}/lib -lx264 -lpthread -lm -ldl" \
        --extra-libs="-lx264 -lpthread -lm -ldl"

    
    info "Building FFmpeg..."
    make -j"${JOBS}"
    
    info "Installing FFmpeg..."
    make install
    
    info "Cleaning FFmpeg build..."
    make clean
    
    log "FFmpeg built successfully!"
}

# Build X11 libraries
build_x11() {
    log "Building X11 libraries..."
    
    local X11_PREFIX="${INSTALL_PREFIX}"
    local X11_BUILD="${BUILD_DIR}/x11"
    
    mkdir -p "${X11_BUILD}"
    cd "${X11_BUILD}"
    
    # Package versions
    declare -A PACKAGES=(
        ["xorgproto"]="2024.1"
        ["xtrans"]="1.5.0"
        ["libXau"]="1.0.11"
        ["libXdmcp"]="1.1.4"
        ["libpthread-stubs"]="0.5"
        ["xcb-proto"]="1.17.0"
        ["libxcb"]="1.17.0"
        ["libX11"]="1.8.7"
        ["libXext"]="1.3.5"
        ["libXrender"]="0.9.11"
        ["libXrandr"]="1.5.4"
        ["libXfixes"]="6.0.1"
    )
    
    package_url() {
        local name=$1
        local version=$2
        local category=$3

        case "${category}" in
            xorg)
                echo "${XORG_RELEASES_URL}/lib/${name}-${version}.tar.xz"
                ;;
            proto)
                echo "${XORG_RELEASES_URL}/proto/${name}-${version}.tar.xz"
                ;;
            xcb)
                echo "${XCB_RELEASES_URL}/${name}-${version}.tar.xz"
                ;;
            *)
                error "Unknown package category: ${category}"
                ;;
        esac
    }

    download_extract_tarball() {
        local name=$1
        local version=$2
        local category=$3
        local tarball="${name}-${version}.tar.xz"
        local url
        url="$(package_url "${name}" "${version}" "${category}")"

        download_file "${url}"
        
        # Extract if not already extracted
        if [ ! -d "${name}-${version}" ]; then
            info "Extracting ${tarball}..."
            tar -xf "${DOWNLOAD_DIR}/${tarball}"
        else
            info "${name}-${version} already extracted, skipping..."
        fi
    }
    
    build_install() {
        local name=$1
        local version=$2
        local dir="${name}-${version}"
        
        info "Building ${name} ${version}..."
        cd "${dir}"
        
        ./configure \
            --prefix="${X11_PREFIX}" \
            --sysconfdir=/etc \
            --localstatedir=/var \
            --enable-static \
            --disable-shared \
            PKG_CONFIG_PATH="${X11_PREFIX}/lib/pkgconfig:${X11_PREFIX}/share/pkgconfig" \
            CFLAGS="-fPIC"
        
        make -j"${JOBS}"
        make install
        make clean
        
        cd ..
        info "${name} installed successfully!"
    }
    
    # Build dependency in order
    download_extract_tarball "xorgproto" "${PACKAGES[xorgproto]}" "proto"
    build_install "xorgproto" "${PACKAGES[xorgproto]}"
    
    download_extract_tarball "xtrans" "${PACKAGES[xtrans]}" "xorg"
    build_install "xtrans" "${PACKAGES[xtrans]}"
    
    download_extract_tarball "libXau" "${PACKAGES[libXau]}" "xorg"
    build_install "libXau" "${PACKAGES[libXau]}"
    
    download_extract_tarball "libXdmcp" "${PACKAGES[libXdmcp]}" "xorg"
    build_install "libXdmcp" "${PACKAGES[libXdmcp]}"
    
    download_extract_tarball "libpthread-stubs" "${PACKAGES[libpthread-stubs]}" "xcb"
    build_install "libpthread-stubs" "${PACKAGES[libpthread-stubs]}"
    
    download_extract_tarball "xcb-proto" "${PACKAGES[xcb-proto]}" "xcb"
    build_install "xcb-proto" "${PACKAGES[xcb-proto]}"
    
    download_extract_tarball "libxcb" "${PACKAGES[libxcb]}" "xcb"
    build_install "libxcb" "${PACKAGES[libxcb]}"
    
    download_extract_tarball "libX11" "${PACKAGES[libX11]}" "xorg"
    build_install "libX11" "${PACKAGES[libX11]}"
    
    download_extract_tarball "libXext" "${PACKAGES[libXext]}" "xorg"
    build_install "libXext" "${PACKAGES[libXext]}"

    download_extract_tarball "libXrender" "${PACKAGES[libXrender]}" "xorg"
    build_install "libXrender" "${PACKAGES[libXrender]}"
    
    download_extract_tarball "libXrandr" "${PACKAGES[libXrandr]}" "xorg"
    build_install "libXrandr" "${PACKAGES[libXrandr]}"
    
    download_extract_tarball "libXfixes" "${PACKAGES[libXfixes]}" "xorg"
    build_install "libXfixes" "${PACKAGES[libXfixes]}"
    
    log "X11 libraries built successfully!"
} 

# Cleanup build directory
cleanup_build_dir() {
    if [ "$KEEP_BUILD" != true ]; then
        log "Cleaning up build directory..."
        rm -rf "${BUILD_DIR}"
        log "Build directory removed"
    else
        info "Keeping build directory at: ${BUILD_DIR}"
    fi
}

# Cleanup download cache
cleanup_downloads() {
    if [ "$KEEP_DOWNLOADS" != true ]; then
        log "Cleaning up download cache..."
        rm -rf "${DOWNLOAD_DIR}"
        log "Download cache removed"
    else
        info "Keeping download cache at: ${DOWNLOAD_DIR}"
    fi
}

# Print summary
print_summary() {
    echo
    echo "=================================================="
    echo "  Build Complete!"
    echo "=================================================="
    echo "Installation locations:"
    echo "  x264:   ${INSTALL_PREFIX}"
    echo "  FFmpeg: ${INSTALL_PREFIX}"
    echo "  X11:    ${INSTALL_PREFIX}"
    echo
    if [ "$KEEP_DOWNLOADS" = true ]; then
        echo "Download cache: ${DOWNLOAD_DIR}"
    fi
    echo "=================================================="
}

# Main build process
main() {
    print_banner
    
    BUILD_X264=true
    BUILD_FFMPEG=true
    BUILD_X11=true
    BUILD_ZLIB=true
    KEEP_BUILD=false
    KEEP_DOWNLOADS=false
    
    while [[ $# -gt 0 ]]; do
        case $1 in
            --only-x264)
                BUILD_FFMPEG=false
                BUILD_X11=false
                BUILD_ZLIB=false
                shift
                ;;
            --only-ffmpeg)
                BUILD_X264=false
                BUILD_X11=false
                BUILD_ZLIB=false
                shift
                ;;
            --only-x11)
                BUILD_X264=false
                BUILD_FFMPEG=false
                BUILD_ZLIB=false
                shift
                ;;
            --only-zlib)
                BUILD_X264=false
                BUILD_FFMPEG=false
                BUILD_X11=false
                shift
                ;;
            --skip-x264)
                BUILD_X264=false
                shift
                ;;
            --skip-ffmpeg)
                BUILD_FFMPEG=false
                shift
                ;;
            --skip-x11)
                BUILD_X11=false
                shift
                ;;
            --skip-zlib)
                BUILD_ZLIB=false
                shift
                ;;
            --keep-build)
                KEEP_BUILD=true
                shift
                ;;
            --keep-downloads)
                KEEP_DOWNLOADS=true
                shift
                ;;
            -h|--help)
                echo "Usage: $0 [OPTIONS]"
                echo
                echo "Online build script for ScreenRecorder dependencies"
                echo
                echo "Expected directory structure:"
                echo "  third_party/x264/    - x264 source code"
                echo "  third_party/FFmpeg/  - FFmpeg source code"
                echo
                echo "X11 source tarballs are downloaded from upstream X.Org and XCB release servers"
                echo
                echo "Options:"
                echo "  --only-x264          Build only x264"
                echo "  --only-ffmpeg        Build only FFmpeg"
                echo "  --only-x11           Build only X11"
                echo "  --only-zlib          Build only zlib"
                echo "  --skip-x264          Skip x264 build"
                echo "  --skip-ffmpeg        Skip FFmpeg build"
                echo "  --skip-x11           Skip X11 build"
                echo "  --skip-zlib          Skip zlib build"
                echo "  --keep-build         Keep build directory after completion"
                echo "  --keep-downloads     Keep downloaded tarballs cache"
                echo "  -h, --help           Show this help message"
                echo
                echo "Environment variables:"
                echo "  XORG_RELEASES_URL    Override X.Org release base URL"
                echo "  XCB_RELEASES_URL     Override XCB release base URL"
                echo
                echo "Example:"
                echo "  XORG_RELEASES_URL=https://www.x.org/releases/individual $0 --only-x11"
                exit 0
                ;;
            *)
                error "Unknown option: $1"
                ;;
        esac
    done
    
    check_dependencies
    
    verify_sources
    setup_dirs
    
    # Build dependencies in order
    if [ "$BUILD_X264" = true ]; then
        build_x264
    fi
    
    if [ "$BUILD_FFMPEG" = true ]; then
        if [ "$BUILD_X264" = false ] && [ ! -d "${INSTALL_PREFIX}" ]; then
            error "FFmpeg requires x264, but x264 is not built and --skip-x264 was used"
        fi
        build_ffmpeg
    fi
    
    if [ "$BUILD_X11" = true ]; then
        build_x11
    fi

    if [ "$BUILD_ZLIB" = true ]; then
        build_zlib
    fi
    
    cleanup_build_dir
    cleanup_downloads
    print_summary
}

# Run main function
main "$@"
