find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Multimedia Test)
if(APPLE)
    enable_language(OBJCXX)
endif()
function(mds_add_qt_editor)
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    qt_add_library(mikudancestudio_frontend_qt STATIC
        "${root}/src/frontend_qt/language.cpp"
        "${root}/src/frontend_qt/audio_output.cpp"
        "${root}/src/frontend_qt/timeline.cpp"
        "${root}/src/frontend_qt/interpolation.cpp"
        "${root}/src/frontend_qt/editor_window.cpp"
        "${root}/src/frontend_qt/editor_panels.cpp"
        "${root}/src/frontend_qt/viewport.cpp"
        "${root}/src/frontend_qt/relative_pointer.cpp"
        "${root}/src/frontend_qt/surface.cpp")
    target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/audio.cpp" "${root}/src/frontend_qt/export.cpp")
    target_include_directories(mikudancestudio_frontend_qt PUBLIC "${root}/src/frontend_qt")
    target_link_libraries(mikudancestudio_frontend_qt PUBLIC mikudancestudio_editor_core mikudancestudio_scene_render Qt6::Widgets Qt6::Multimedia)
    if(NOT MSVC)
        target_compile_options(mikudancestudio_frontend_qt PRIVATE -ffp-contract=off)
    endif()
    if(MSVC)
        target_compile_options(mikudancestudio_frontend_qt PRIVATE /utf-8 /fp:precise)
    elseif(APPLE)
        target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/surface_mac.mm")
        set_target_properties(mikudancestudio_frontend_qt PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(mikudancestudio_frontend_qt PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        target_link_libraries(mikudancestudio_frontend_qt PRIVATE "-framework Cocoa" "-framework QuartzCore")
    else()
        # The small platform adapter uses QPA to retrieve Wayland wl_surface.
        # Keep the Qt minor version pinned in CI and test both X11 and Wayland.
        target_link_libraries(mikudancestudio_frontend_qt PRIVATE Qt6::GuiPrivate)
        enable_language(C)
        find_package(Qt6 6.8 REQUIRED COMPONENTS WaylandClient)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MDS_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
        pkg_get_variable(MDS_WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
        if(NOT MDS_WAYLAND_PROTOCOLS)
            message(FATAL_ERROR "The Qt editor requires the wayland-protocols development package")
        endif()
        if(NOT TARGET Wayland::Scanner)
            find_program(MDS_WAYLAND_SCANNER wayland-scanner REQUIRED)
            add_executable(Wayland::Scanner IMPORTED)
            set_target_properties(Wayland::Scanner PROPERTIES IMPORTED_LOCATION "${MDS_WAYLAND_SCANNER}")
        endif()
        target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/relative_pointer_wayland.cpp")
        target_link_libraries(mikudancestudio_frontend_qt PRIVATE Qt6::WaylandClient PkgConfig::MDS_WAYLAND)
        qt_generate_wayland_protocol_client_sources(mikudancestudio_frontend_qt PRIVATE_CODE FILES
            "${MDS_WAYLAND_PROTOCOLS}/unstable/relative-pointer/relative-pointer-unstable-v1.xml"
            "${MDS_WAYLAND_PROTOCOLS}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")
    endif()
    qt_add_resources(mikudancestudio_frontend_qt editor_translations
        PREFIX "/i18n" BASE "${root}/res/i18n"
        FILES "${root}/res/i18n/en.json" "${root}/res/i18n/ja.json" "${root}/res/i18n/zh_CN.json")
    qt_add_executable(MikuDanceStudioNG "${root}/src/frontend_qt/main.cpp")
    target_link_libraries(MikuDanceStudioNG PRIVATE mikudancestudio_frontend_qt)
    add_custom_command(TARGET MikuDanceStudioNG POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${root}/res/assets" "$<TARGET_FILE_DIR:MikuDanceStudioNG>/assets")
    if(BUILD_TESTING)
        if(UNIX AND NOT APPLE)
            add_subdirectory("${root}/cmake/qt-wayland-pointer-tests" "${CMAKE_CURRENT_BINARY_DIR}/wayland-pointer")
        endif()
        add_executable(editor_overlay_test "${root}/tests/editor_overlay_test.cpp")
        target_link_libraries(editor_overlay_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render)
        add_test(NAME editor_overlay COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:editor_overlay_test> --assets "${root}/res/assets")
        set_tests_properties(editor_overlay PROPERTIES LABELS "p4;offscreen;gpu" TIMEOUT 120)
        add_executable(editor_view_test "${root}/tests/editor_view_test.cpp")
        target_link_libraries(editor_view_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render)
        add_test(NAME editor_view COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py" --test $<TARGET_FILE:editor_view_test>)
        set_tests_properties(editor_view PROPERTIES LABELS "p4;headless;portable" TIMEOUT 120)
        add_executable(portable_uv_render_test "${root}/tests/portable_uv_render_test.cpp")
        target_link_libraries(portable_uv_render_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render)
        add_test(NAME portable_uv_render COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:portable_uv_render_test> --assets "${root}/res/assets")
        set_tests_properties(portable_uv_render PROPERTIES LABELS "p4;offscreen;gpu" TIMEOUT 120)
        qt_add_executable(qt_bone_drag_test "${root}/tests/qt_bone_drag_test.cpp")
        target_link_libraries(qt_bone_drag_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_bone_drag COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_bone_drag_test> --assets "${root}/res/assets")
        set_tests_properties(qt_bone_drag PROPERTIES LABELS "p4;offscreen;qt" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_audio_test "${root}/tests/qt_audio_test.cpp")
        target_link_libraries(qt_audio_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_audio COMMAND qt_audio_test "${root}/res/assets")
        set_tests_properties(qt_audio PROPERTIES LABELS "p4;offscreen;qt;audio" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_editor_test "${root}/tests/qt_editor_test.cpp")
        target_link_libraries(qt_editor_test PRIVATE mikudancestudio_frontend_qt Qt6::Test)
        add_test(NAME qt_editor COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_editor_test> --assets "${root}/res/assets")
        set_tests_properties(qt_editor PROPERTIES LABELS "p4;offscreen;qt" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_editor_render_test "${root}/tests/qt_editor_render_test.cpp")
        target_link_libraries(qt_editor_render_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_editor_render COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_editor_render_test> --assets "${root}/res/assets")
        set_tests_properties(qt_editor_render PROPERTIES LABELS "p4;offscreen;qt;gpu" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_presentation_test "${root}/tests/qt_presentation_test.cpp")
        target_link_libraries(qt_presentation_test PRIVATE mikudancestudio_frontend_qt)
        if(WIN32)
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                add_executable(editor_overlay_oracle_test EXCLUDE_FROM_ALL "${root}/tests/editor_overlay_oracle_test.cpp")
                target_compile_definitions(editor_overlay_oracle_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
                target_link_libraries(editor_overlay_oracle_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render bcrypt)
            endif()
            qt_add_executable(qt_timeline_gdi_test "${root}/tests/qt_timeline_gdi_test.cpp")
            target_link_libraries(qt_timeline_gdi_test PRIVATE mikudancestudio_frontend_qt gdi32 user32)
            add_test(NAME qt_timeline_gdi COMMAND qt_timeline_gdi_test "${root}/res/assets")
            set_tests_properties(qt_timeline_gdi PROPERTIES LABELS "p4;offscreen;qt;oracle" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
            # Explicit hidden DX9 oracle; never required on machines without a
            # connected DX9 session. D3DX is loaded only inside this test.
            add_executable(portable_scene_dx9_test "${root}/tests/portable_scene_dx9_test.cpp")
            target_include_directories(portable_scene_dx9_test PRIVATE "${root}/third_party/mmeffect/include")
            target_compile_definitions(portable_scene_dx9_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
            target_link_libraries(portable_scene_dx9_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render d3d9)
            add_executable(portable_background_dx9_test "${root}/tests/portable_background_dx9_test.cpp")
            target_include_directories(portable_background_dx9_test PRIVATE "${root}/third_party/mmeffect/include")
            target_compile_definitions(portable_background_dx9_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
            target_link_libraries(portable_background_dx9_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render d3d9)
        endif()
        # Native platform tests are invoked explicitly in isolated CI displays;
        # the normal p4 suite must remain safe for a developer's desktop.
    endif()
endfunction()
