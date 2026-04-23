# Standalone boot splash — small DRM + libpng binary, no LVGL dependency.
# Shown from systemd sysinit.target until the main gocontroll-hmi-demo
# service is ready to grab DRM master.
#
# This directory is self-contained; when built from the top-level project
# CMakeLists.txt it inherits the parent's pkg_check_modules results for
# libdrm / libpng.

add_executable(gocontroll-hmi-splash
    src/main.c
    src/splash_drm.c
    src/splash_png.c
)

target_include_directories(gocontroll-hmi-splash PRIVATE
    src
    ${LIBDRM_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS}
)

target_link_libraries(gocontroll-hmi-splash PRIVATE
    ${LIBDRM_LIBRARIES}
    ${PNG_LIBRARIES}
    m
)

target_link_options(gocontroll-hmi-splash PRIVATE -Wl,--gc-sections)

install(TARGETS gocontroll-hmi-splash RUNTIME DESTINATION bin)
