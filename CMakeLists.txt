cmake_minimum_required(VERSION 3.15)
project(gocontroll-hmi-demo C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# The version the Information screen shows. debian/changelog is the single
# source of truth — the .deb version already comes from there, so parse it
# rather than keeping a second copy here that silently drifts. Builds from a
# tree without debian/ (a bare cmake checkout) fall back to a marker that is
# obviously not a release.
set(HMI_DEMO_VERSION "0.0.0-dev")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/debian/changelog" _changelog_head LIMIT_COUNT 1)
    if(_changelog_head MATCHES "^[^ ]+ \\(([^)]+)\\)")
        set(HMI_DEMO_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()
message(STATUS "HMI demo version: ${HMI_DEMO_VERSION}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Aggressive but safe optimizations for Cortex-A53 (i.MX 8M Plus)
add_compile_options(
    -Wall -Wextra
    -Wno-unused-parameter
    -fno-plt
    -ffunction-sections -fdata-sections
)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -mtune=cortex-a53)
endif()

# CMake option for the GLES 2.0 backend
option(USE_OPENGLES "Compose LVGL output on the GPU via GLES 2.0/EGL/GBM" OFF)

# LVGL config discovery — it includes ../../lv_conf.h relative to itself
set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h" CACHE FILEPATH "Path to lv_conf.h")
add_definitions(-DLV_CONF_INCLUDE_SIMPLE=1)
include_directories(${CMAKE_SOURCE_DIR})

if(USE_OPENGLES)
    add_compile_definitions(GOCONTROLL_USE_OPENGLES=1)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON  REQUIRED libcjson)
pkg_check_modules(LIBDRM REQUIRED libdrm)
pkg_check_modules(EVDEV  REQUIRED libevdev)
pkg_check_modules(PNG    REQUIRED libpng)

# LVGL's lv_linux_drm.c pulls in <drm.h> (from libdrm), and lv_libpng uses
# <png.h>. Add those include dirs globally so the LVGL subtree can compile.
include_directories(SYSTEM ${LIBDRM_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS} ${EVDEV_INCLUDE_DIRS})

add_subdirectory(thirdparty/lvgl)

add_executable(gocontroll-hmi-demo
    src/main.c
    src/ui.c
    src/ipc.c
    src/config.c
    src/controller_pins.c
    src/drm_display.c
    src/product_info.c
    src/settings.c
    src/shm_data.c
    src/sysinfo.c
    src/identify.c
    src/module_pins.c
    src/fonts/lv_font_montserrat_14_ext.c
)

target_compile_definitions(gocontroll-hmi-demo PRIVATE
    GOCONTROLL_HMI_DEMO_VERSION="${HMI_DEMO_VERSION}"
)

target_include_directories(gocontroll-hmi-demo PRIVATE
    src
    ${CJSON_INCLUDE_DIRS}
    ${LIBDRM_INCLUDE_DIRS}
    ${EVDEV_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
)

target_link_libraries(gocontroll-hmi-demo PRIVATE
    lvgl::lvgl
    ${CJSON_LIBRARIES}
    ${LIBDRM_LIBRARIES}
    ${EVDEV_LIBRARIES}
    ${PNG_LIBRARIES}
    m
)

target_link_options(gocontroll-hmi-demo PRIVATE -Wl,--gc-sections)

install(TARGETS gocontroll-hmi-demo RUNTIME DESTINATION bin)

# Standalone early-boot splash binary (no LVGL dependency).
add_subdirectory(splash)
