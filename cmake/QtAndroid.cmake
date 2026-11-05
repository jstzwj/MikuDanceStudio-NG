# Android uses Qt's APK deployment, never the desktop install/deploy API.
target_compile_definitions(MikuDanceStudioNG PRIVATE MDS_PRODUCT_VERSION="${MDS_VERSION}")
target_compile_definitions(mikudancestudio_frontend_qt PUBLIC MDS_PRODUCT_VERSION="${MDS_VERSION}")
target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/surface_android.cpp"
    "${root}/src/frontend_qt/android_files.cpp" "${root}/src/frontend_qt/android_acceptance.cpp")
set(mds_android_package "${CMAKE_CURRENT_BINARY_DIR}/android-package")
file(MAKE_DIRECTORY "${mds_android_package}")
file(MAKE_DIRECTORY "${mds_android_package}/res/drawable")
configure_file("${root}/res/frontend_qt/android/icon.png" "${mds_android_package}/res/drawable/icon.png" COPYONLY)
set(MDS_RELEASE_TAG "" CACHE STRING "Exact version tag for release APKs; empty for development")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${root}/tools/android_build_info.py"
    --source "${root}" --output "${mds_android_package}/assets/build-info.json"
    --abi "${ANDROID_ABI}" --qt "${Qt6_VERSION}" --tag "${MDS_RELEASE_TAG}" --abis ${QT_ANDROID_ABIS}
    RESULT_VARIABLE mds_android_identity_error)
if(NOT mds_android_identity_error EQUAL 0)
    message(FATAL_ERROR "Cannot establish Android package identity")
endif()
file(COPY "${root}/LICENSE" "${root}/licenses/MikuMikuDance.txt" "${root}/docs/THIRD_PARTY_RUNTIME.md"
    "${root}/res/fonts/NotoSansCJK-OFL.txt" DESTINATION "${mds_android_package}/assets/licenses")
file(COPY "${root}/res/licenses/Qt" DESTINATION "${mds_android_package}/assets/licenses")
file(COPY "${root}/recipes/bullet275/LICENSE.txt" DESTINATION "${mds_android_package}/assets/licenses/Bullet")
file(COPY "${root}/third_party/stb/stb_image.h" DESTINATION "${mds_android_package}/assets/licenses/stb")
configure_file("${root}/res/frontend_qt/android/AndroidManifest.xml.in"
    "${mds_android_package}/AndroidManifest.xml" @ONLY)
file(GLOB_RECURSE mds_android_assets CONFIGURE_DEPENDS "${root}/res/assets/*")
qt_add_resources(MikuDanceStudioNG android_assets PREFIX "/android/assets" BASE "${root}/res/assets"
    FILES ${mds_android_assets})
# A single ABI-independent APK asset avoids compiling and embedding the same
# 19 MB collection into both native libraries (and evicting useful object cache).
file(MAKE_DIRECTORY "${mds_android_package}/assets/fonts")
configure_file("${MDS_EDITOR_FONT_FILE}"
    "${mds_android_package}/assets/fonts/NotoSansCJK-Regular.ttc" COPYONLY)
if(MDS_VERSION_MINOR GREATER 999 OR MDS_VERSION_PATCH GREATER 999)
    message(FATAL_ERROR "Android's version code requires minor/patch < 1000")
endif()
math(EXPR mds_android_version_code "${MDS_VERSION_MAJOR} * 1000000 + ${MDS_VERSION_MINOR} * 1000 + ${MDS_VERSION_PATCH}")
set_target_properties(MikuDanceStudioNG PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${mds_android_package}"
    QT_ANDROID_PACKAGE_NAME "io.github.jstzwj.mikudancestudiong"
    QT_ANDROID_APP_NAME "MikuDanceStudio-NG"
    QT_ANDROID_VERSION_NAME "${MDS_VERSION}"
    QT_ANDROID_VERSION_CODE "${mds_android_version_code}"
    QT_ANDROID_MIN_SDK_VERSION 33
    QT_ANDROID_TARGET_SDK_VERSION 35)
