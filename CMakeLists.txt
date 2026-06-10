cmake_minimum_required(VERSION 3.24)
project(Daemonitor VERSION 0.1.0 LANGUAGES CXX)

# Architecture per "程序设计之书" (Codex of Program Design) v0.1.0
#   src/functions/system/   — cross-cutting system capabilities     (Codex §6.4)
#     app-core-sys/         — startup, lifecycle, kernel             (Codex §6.4)
#     monitor-sys/          — CPU/GPU/RAM/NET/Disk/Power monitors    (Codex §6.4)
#     print-sys/            — terminal formatting layer              (Codex §6.4)
#     display-sys/          — Direct2D + DirectWrite overlay    (Codex §6.4)
#     tray-sys/             — Shell_NotifyIcon system tray           (Codex §6.4)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sources ─────────────────────────────────────────
file(GLOB_RECURSE DAEMONITOR_SOURCES
    src/functions/system/app-core-sys/*.cpp
    src/functions/system/monitor-sys/*.cpp
    src/functions/system/print-sys/*.cpp
    src/functions/system/display-sys/*.cpp
    src/functions/system/tray-sys/*.cpp
)

# ── Resources (Icons, Version Info) ─────────────────
set(DAEMONITOR_RESOURCES
    src/resources/daemonitor.rc
)

# ── Executable (WIN32 = no console window) ────────
add_executable(${PROJECT_NAME} WIN32
    ${DAEMONITOR_SOURCES}
    ${DAEMONITOR_RESOURCES}
)

# ── Output Naming (per KEYMERA versioning scheme) ────
#   {Project}-{version}-{stage}.{N}  →  Daemonitor-0.1.0-dev.1
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-dev.1"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    src/resources
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pdh
    dxgi
    iphlpapi
    wbemuuid
    ole32
    oleaut32
    d2d1
    dwrite
    gdi32
    shell32
)

# ═══════════════════════════════════════════════════════════════
# Build profile — shared configuration for all targets
# ═══════════════════════════════════════════════════════════════
function(daemonitor_configure_target TARGET)
    if(MINGW)
        target_compile_options(${TARGET} PRIVATE
            -Os
            -fno-ident
            -fomit-frame-pointer
            -ffunction-sections
            -fdata-sections
        )
        target_link_options(${TARGET} PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
            -Wl,-s
            -Wl,--build-id=none
        )
    endif()

    if(MSVC)
        target_compile_options(${TARGET} PRIVATE
            /arch:AVX2
            /O2
            /MT
            /W4
        )
    endif()
endfunction()

daemonitor_configure_target(${PROJECT_NAME})
