# =========================
# libnfs
# =========================
include(FetchContent)

set(ENABLE_GNUTLS     OFF)
set(WITH_GNUTLS       OFF)
set(BUILD_SHARED_LIBS OFF)
set(ENABLE_TESTS      OFF)
set(ENABLE_EXAMPLES   OFF)
set(ENABLE_UTILS      OFF)

FetchContent_Declare(
  libnfs
  URL "https://github.com/sahlberg/libnfs/archive/refs/tags/libnfs-6.0.2.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP NO
)

FetchContent_MakeAvailable(libnfs)