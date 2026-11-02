# Called from mds_add_qt_editor, after the application target exists.
include(GNUInstallDirs)
file(STRINGS "${root}/VERSION" mds_product_version LIMIT_COUNT 1)
target_compile_definitions(MikuDanceStudioNG PRIVATE MDS_PRODUCT_VERSION="${mds_product_version}")
set_target_properties(MikuDanceStudioNG PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.jstzwj.MikuDanceStudioNG"
    MACOSX_BUNDLE_BUNDLE_NAME "MikuDanceStudio-NG"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${mds_product_version}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${mds_product_version}")
if(WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
    set(CMAKE_INSTALL_LIBDIR ".")
    # Resolve the toolchain's redistributables at configure time, including when
    # the dist target runs outside a Visual Studio developer shell.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
    include(InstallRequiredSystemLibraries)
elseif(APPLE)
    set_target_properties(MikuDanceStudioNG PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
else()
    set(CMAKE_INSTALL_LIBDIR "lib")
    set_target_properties(MikuDanceStudioNG PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
install(TARGETS MikuDanceStudioNG
    BUNDLE DESTINATION . COMPONENT Runtime
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)
if(APPLE)
    set(mds_assets_dir "MikuDanceStudioNG.app/Contents/Resources/assets")
else()
    set(mds_assets_dir "${CMAKE_INSTALL_BINDIR}/assets")
endif()
install(DIRECTORY "${root}/res/assets/" DESTINATION "${mds_assets_dir}" COMPONENT Runtime)
install(FILES "${root}/LICENSE" DESTINATION licenses COMPONENT Runtime)
install(FILES "${root}/recipes/bullet275/LICENSE.txt" DESTINATION licenses/Bullet COMPONENT Runtime)
install(FILES "${root}/docs/DISTRIBUTION.md" DESTINATION . COMPONENT Runtime)
set(deploy_args)
if(WIN32)
    # Keep the compiler runtime beside the application, not a prerequisite installer.
    set(deploy_args DEPLOY_TOOL_OPTIONS --release --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler)
endif()
qt6_generate_deploy_app_script(TARGET MikuDanceStudioNG OUTPUT_SCRIPT deploy_script ${deploy_args})
install(CODE "set(QT_DEPLOY_BIN_DIR \"${CMAKE_INSTALL_BINDIR}\")
set(QT_DEPLOY_LIB_DIR \"${CMAKE_INSTALL_LIBDIR}\")" COMPONENT Runtime)
install(SCRIPT "${deploy_script}" COMPONENT Runtime)
get_target_property(mds_qmake Qt6::qmake IMPORTED_LOCATION)
if(NOT mds_qmake)
    get_target_property(mds_qmake Qt6::qmake IMPORTED_LOCATION_RELEASE)
endif()
set(MDS_MOLTENVK_LIBRARY "" CACHE FILEPATH "MoltenVK dylib to include in the macOS distribution")
set(MDS_DXC_RUNTIME_DIR "" CACHE PATH "Pinned DXC distribution (defaults to the configured compiler's parent)")
set(mds_dist_metal false)
if(MIKUDANCESTUDIO_RHI_METAL)
    set(mds_dist_metal true)
    FetchContent_GetProperties(spirv_cross SOURCE_DIR mds_spirv_cross_source)
    install(FILES "${mds_spirv_cross_source}/LICENSE" DESTINATION licenses/SPIRV-Cross COMPONENT Runtime)
endif()
set(mds_dist_vulkan false)
if(MIKUDANCESTUDIO_RHI_VULKAN)
    set(mds_dist_vulkan true)
endif()
configure_file("${root}/cmake/qt-dist-config.json.in" "${CMAKE_BINARY_DIR}/qt-dist-config.json" @ONLY)
add_custom_target(dist
    COMMAND "${Python3_EXECUTABLE}" "${root}/tools/package_editor.py"
        --build "${CMAKE_BINARY_DIR}" --config "$<CONFIG>" --cmake "${CMAKE_COMMAND}"
    DEPENDS MikuDanceStudioNG
    COMMENT "Deploy Qt, renderer runtimes and assets; create dist and its archive"
    VERBATIM)
