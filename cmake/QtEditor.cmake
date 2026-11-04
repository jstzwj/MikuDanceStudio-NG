find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Multimedia Test)
if(WIN32)
    enable_language(RC)
elseif(APPLE)
    # Qt 6.8.3 links AGL even when the selected SDK no longer contains it.
    # Qt does not use this obsolete framework; upstream removed the dependency:
    # https://github.com/qt/qtbase/commit/cdb33c3d5621ce035ad6950c8e2268fe94b73de5
    # A host framework outside that SDK can still satisfy find_library. Remove
    # both that path and the literal fallback, retaining all other dependencies.
    if(TARGET WrapOpenGL::WrapOpenGL)
        get_target_property(mds_qt_gl_links WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        set(mds_qt_gl_original "${mds_qt_gl_links}")
        list(FILTER mds_qt_gl_links EXCLUDE REGEX "(^-framework AGL$)|(/AGL\\.framework(/AGL)?$)")
        if(NOT mds_qt_gl_links STREQUAL mds_qt_gl_original)
            set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${mds_qt_gl_links}")
            message(STATUS "Qt: removed obsolete AGL dependency (${mds_qt_gl_original})")
        endif()
    endif()
    enable_language(OBJCXX)
elseif(UNIX)
    # Protocol metadata is C. Enable it in directory scope so CMake retains
    # the language rules when generating targets after the function returns.
    enable_language(C)
endif()
function(mds_add_qt_editor)
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    qt_add_library(mikudancestudio_frontend_qt STATIC
        "${root}/src/frontend_qt/language.cpp"
        "${root}/src/frontend_qt/audio_output.cpp"
        "${root}/src/frontend_qt/timeline.cpp"
        "${root}/src/frontend_qt/interpolation.cpp"
        "${root}/src/frontend_qt/editor_window.cpp"
        "${root}/src/frontend_qt/editor_canvas.cpp"
        "${root}/src/frontend_qt/editor_viewport.cpp"
        "${root}/src/frontend_qt/editor_files.cpp"
        "${root}/src/frontend_qt/editor_dialogs.cpp"
        "${root}/src/frontend_qt/editor_input.cpp"
        "${root}/src/frontend_qt/editor_font.cpp"
        "${root}/src/frontend_qt/editor_style.cpp"
        "${root}/src/frontend_qt/editor_menus.cpp"
        "${root}/src/frontend_qt/editor_panels.cpp"
        "${root}/src/frontend_qt/numeric_dialog.cpp"
        "${root}/src/frontend_qt/english_name_dialog.cpp"
        "${root}/src/frontend_qt/toon_dialog.cpp"
        "${root}/src/frontend_qt/model_physics_dialog.cpp"
        "${root}/src/frontend_qt/viewport.cpp"
        "${root}/src/frontend_qt/relative_pointer.cpp"
        "${root}/src/frontend_qt/surface.cpp")
    target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/audio.cpp" "${root}/src/frontend_qt/export.cpp")
    target_include_directories(mikudancestudio_frontend_qt PUBLIC "${root}/src/frontend_qt")
    target_link_libraries(mikudancestudio_frontend_qt PUBLIC mikudancestudio_editor_core mikudancestudio_scene_render Qt6::Widgets Qt6::Multimedia)
    if(WIN32)
        target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/editor_style_windows.cpp")
        target_link_libraries(mikudancestudio_frontend_qt PRIVATE gdi32 uxtheme user32)
    endif()
    if(NOT MSVC)
        target_compile_options(mikudancestudio_frontend_qt PRIVATE -ffp-contract=off)
    endif()
    if(MSVC)
        target_compile_options(mikudancestudio_frontend_qt PRIVATE /utf-8 /fp:precise)
    elseif(APPLE)
        target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/surface_mac.mm"
            "${root}/src/frontend_qt/audio_output_coreaudio.cpp")
        set_target_properties(mikudancestudio_frontend_qt PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(mikudancestudio_frontend_qt PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
        target_link_libraries(mikudancestudio_frontend_qt PRIVATE "-framework Cocoa" "-framework QuartzCore"
            "-framework AudioToolbox")
    else()
        # The small platform adapter uses QPA to retrieve Wayland wl_surface.
        # Keep the Qt minor version pinned in CI and test both X11 and Wayland.
        target_link_libraries(mikudancestudio_frontend_qt PRIVATE Qt6::GuiPrivate)
        find_package(Qt6 6.8 REQUIRED COMPONENTS WaylandClient)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MDS_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            pkg_check_modules(MDS_PULSE REQUIRED IMPORTED_TARGET libpulse>=13)
            target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/audio_output_pulse.cpp")
            target_link_libraries(mikudancestudio_frontend_qt PRIVATE PkgConfig::MDS_PULSE)
        endif()
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
    qt_add_resources(MikuDanceStudioNG application_icon
        PREFIX "/icons" BASE "${root}/res/assets"
        FILES "${root}/res/assets/app.ico")
    if(WIN32)
        configure_file("${root}/res/frontend_qt/windows.rc.in"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/qt-editor-icon.rc" @ONLY)
        target_sources(MikuDanceStudioNG PRIVATE "${root}/res/frontend_qt/windows.manifest"
            "${CMAKE_CURRENT_BINARY_DIR}/generated/qt-editor-icon.rc")
    endif()
    target_link_libraries(MikuDanceStudioNG PRIVATE mikudancestudio_frontend_qt)
    include("${root}/cmake/EditorFonts.cmake")
    add_custom_command(TARGET MikuDanceStudioNG POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${root}/res/assets" "$<TARGET_FILE_DIR:MikuDanceStudioNG>/assets")
    include("${root}/cmake/QtDistribution.cmake")
    if(BUILD_TESTING)
        qt_add_executable(qt_language_switch_test "${root}/tests/qt_language_switch_test.cpp")
        target_link_libraries(qt_language_switch_test PRIVATE mikudancestudio_frontend_qt)
        if(WIN32)
            target_sources(qt_language_switch_test PRIVATE "${root}/res/frontend_qt/windows.manifest")
            add_test(NAME qt_language_switch_native COMMAND ${Python3_EXECUTABLE}
                "${root}/tests/test_qt_language_switch.py" --test $<TARGET_FILE:qt_language_switch_test>
                --assets "${root}/res/assets")
            set_tests_properties(qt_language_switch_native PROPERTIES LABELS "p4;hidden;qt;language"
                TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=windows")
        endif()
        add_test(NAME qt_language_switch COMMAND ${Python3_EXECUTABLE}
            "${root}/tests/test_qt_language_switch.py" --test $<TARGET_FILE:qt_language_switch_test>
            --assets "${root}/res/assets")
        set_tests_properties(qt_language_switch PROPERTIES LABELS "p4;offscreen;qt;language"
            TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_editor_chrome_test "${root}/tests/qt_editor_chrome_test.cpp")
        target_link_libraries(qt_editor_chrome_test PRIVATE mikudancestudio_frontend_qt)
        if(WIN32)
            target_sources(qt_editor_chrome_test PRIVATE "${root}/res/frontend_qt/windows.manifest")
        endif()
        add_test(NAME qt_editor_chrome COMMAND qt_editor_chrome_test "${root}/res/assets" "${CMAKE_BINARY_DIR}/editor-chrome")
        set_tests_properties(qt_editor_chrome PROPERTIES LABELS "p4;offscreen;qt;appearance"
            TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_editor_font_test "${root}/tests/qt_editor_font_test.cpp")
        target_link_libraries(qt_editor_font_test PRIVATE mikudancestudio_frontend_qt)
        qt_add_executable(qt_system_ime_test EXCLUDE_FROM_ALL "${root}/tests/qt_system_ime_test.cpp")
        target_link_libraries(qt_system_ime_test PRIVATE mikudancestudio_frontend_qt)
        if(APPLE)
            target_sources(qt_system_ime_test PRIVATE "${root}/tests/system_ime_macos.mm")
            target_compile_options(qt_system_ime_test PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
            target_link_libraries(qt_system_ime_test PRIVATE "-framework AppKit" "-framework Carbon")
        endif()
        if(WIN32)
            target_sources(qt_system_ime_test PRIVATE "${root}/res/frontend_qt/windows.manifest")
            target_link_libraries(qt_system_ime_test PRIVATE ole32 uuid imm32 dbghelp)
            qt_add_executable(qt_control_style_test "${root}/tests/qt_control_style_oracle_test.cpp"
                "${root}/res/frontend_qt/windows.manifest" "${root}/res/templates/dialog/dialogs.rc")
            target_compile_definitions(qt_control_style_test PRIVATE MDS_SYSTEM_CONTROL_REFERENCE
                MDS_EDIT_LAYOUT_REFERENCE="${root}/tests/reference/original-edit-layouts.json")
            target_link_libraries(qt_control_style_test PRIVATE mikudancestudio_frontend_qt gdi32 user32 comctl32 uxtheme)
            add_test(NAME qt_control_style COMMAND qt_control_style_test "${CMAKE_BINARY_DIR}/control-style")
            set_tests_properties(qt_control_style PROPERTIES LABELS "p4;hidden;qt;appearance" TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=windows")
            add_test(NAME qt_control_text COMMAND qt_control_style_test --text "${CMAKE_BINARY_DIR}/control-text")
            set_tests_properties(qt_control_text PROPERTIES LABELS "p4;hidden;qt;appearance" TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=windows")
            add_test(NAME qt_list_content COMMAND qt_control_style_test --content "${CMAKE_BINARY_DIR}/list-content")
            set_tests_properties(qt_list_content PROPERTIES LABELS "p4;hidden;qt;appearance" TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=windows")
            add_test(NAME qt_edit_content COMMAND qt_control_style_test --edit-text "${CMAKE_BINARY_DIR}/edit-content")
            set_tests_properties(qt_edit_content PROPERTIES LABELS "p4;hidden;qt;appearance" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=windows")
            target_link_libraries(qt_editor_font_test PRIVATE gdi32)
            qt_add_executable(qt_native_input_test "${root}/tests/qt_native_input_test.cpp")
            target_link_libraries(qt_native_input_test PRIVATE mikudancestudio_frontend_qt user32)
            add_test(NAME qt_native_input COMMAND ${Python3_EXECUTABLE}
                "${root}/tests/test_runtime_cpu_scene.py"
                --passes $<TARGET_FILE:qt_native_input_test> --assets "${root}/res/assets")
            set_tests_properties(qt_native_input PROPERTIES LABELS "p4;hidden;qt;input"
                TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=windows")
        endif()
        add_test(NAME qt_editor_font COMMAND qt_editor_font_test "${root}/res/assets")
        set_tests_properties(qt_editor_font PROPERTIES LABELS "p4;offscreen;qt;font" TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_c1_workflow_test "${root}/tests/qt_c1_workflow_test.cpp")
        target_link_libraries(qt_c1_workflow_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_c1_workflow COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_c1_workflow_test> --assets "${root}/res/assets")
        set_tests_properties(qt_c1_workflow PROPERTIES LABELS "p4;offscreen;qt;gpu;c1" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_texture_reload_test "${root}/tests/qt_texture_reload_test.cpp")
        target_link_libraries(qt_texture_reload_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_texture_reload COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_texture_reload_test> --assets "${root}/res/assets")
        set_tests_properties(qt_texture_reload PROPERTIES LABELS "p4;offscreen;qt;gpu" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
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
        # Explicit opt-in only: this test opens a native audio device and feeds
        # silence. It is intentionally absent from the ordinary CTest suite.
        qt_add_executable(qt_native_audio_test "${root}/tests/qt_native_audio_test.cpp")
        target_link_libraries(qt_native_audio_test PRIVATE mikudancestudio_frontend_qt)
        qt_add_executable(qt_editor_test "${root}/tests/qt_editor_test.cpp")
        qt_add_executable(qt_numeric_test "${root}/tests/qt_numeric_test.cpp")
        qt_add_executable(qt_english_names_test "${root}/tests/qt_english_names_test.cpp")
        qt_add_executable(qt_model_save_test "${root}/tests/qt_model_save_test.cpp")
        qt_add_executable(qt_model_physics_test "${root}/tests/qt_model_physics_test.cpp")
        target_link_libraries(qt_model_physics_test PRIVATE mikudancestudio_frontend_qt Qt6::Test)
        add_test(NAME qt_model_physics COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_qt_numeric.py"
            --test $<TARGET_FILE:qt_model_physics_test> --assets "${root}/res/assets"
            --evidence "${root}/tests/reference/model-physics-original.json")
        set_tests_properties(qt_model_physics PROPERTIES LABELS "p4;offscreen;qt;oracle" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_toon_test "${root}/tests/qt_toon_test.cpp")
        target_link_libraries(qt_toon_test PRIVATE mikudancestudio_frontend_qt Qt6::Test)
        add_test(NAME qt_toon COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_qt_numeric.py"
            --test $<TARGET_FILE:qt_toon_test> --assets "${root}/res/assets")
        set_tests_properties(qt_toon PROPERTIES LABELS "p4;offscreen;qt;oracle" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        target_link_libraries(qt_model_save_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_model_save COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_model_save_test> --assets "${root}/res/assets")
        set_tests_properties(qt_model_save PROPERTIES LABELS "p4;offscreen;qt" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        target_link_libraries(qt_english_names_test PRIVATE mikudancestudio_frontend_qt Qt6::Test)
        add_test(NAME qt_english_names COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_qt_numeric.py"
            --test $<TARGET_FILE:qt_english_names_test> --assets "${root}/res/assets")
        set_tests_properties(qt_english_names PROPERTIES LABELS "p4;offscreen;qt;oracle" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        target_link_libraries(qt_numeric_test PRIVATE mikudancestudio_frontend_qt Qt6::Test)
        add_test(NAME qt_numeric COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_qt_numeric.py"
            --test $<TARGET_FILE:qt_numeric_test> --assets "${root}/res/assets")
        set_tests_properties(qt_numeric PROPERTIES LABELS "p4;offscreen;qt;oracle" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_menu_test "${root}/tests/qt_menu_test.cpp")
        qt_add_executable(qt_file_drop_test "${root}/tests/qt_file_drop_test.cpp")
        target_link_libraries(qt_file_drop_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_file_drop COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_file_drop_test> --assets "${root}/res/assets")
        set_tests_properties(qt_file_drop PROPERTIES LABELS "p4;offscreen;qt" TIMEOUT 120
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        target_link_libraries(qt_menu_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_menu COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_qt_menu.py"
            --test $<TARGET_FILE:qt_menu_test> --assets "${root}/res/assets")
        set_tests_properties(qt_menu PROPERTIES LABELS "p4;offscreen;qt;oracle" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        target_link_libraries(qt_editor_test PRIVATE mikudancestudio_frontend_qt Qt6::Test)
        add_test(NAME qt_editor COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_editor_test> --assets "${root}/res/assets")
        set_tests_properties(qt_editor PROPERTIES LABELS "p4;offscreen;qt" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_channel_binding_test "${root}/tests/qt_channel_binding_test.cpp")
        target_link_libraries(qt_channel_binding_test PRIVATE mikudancestudio_frontend_qt Qt6::Test)
        add_test(NAME qt_channel_binding COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_channel_binding_test> --assets "${root}/res/assets")
        set_tests_properties(qt_channel_binding PROPERTIES LABELS "p4;offscreen;qt" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_editor_performance_test "${root}/tests/qt_editor_performance_test.cpp")
        if(NOT TARGET scene_frame_producer_test)
            add_executable(scene_frame_producer_test "${root}/tests/scene_frame_producer_test.cpp")
            target_link_libraries(scene_frame_producer_test PRIVATE mikudancestudio_scene_render)
        endif()
        add_test(NAME p4_scene_frame_contract COMMAND ${Python3_EXECUTABLE}
            "${root}/tests/test_runtime_cpu_scene.py" --passes $<TARGET_FILE:scene_frame_producer_test>
            --assets "${root}/res/assets")
        set_tests_properties(p4_scene_frame_contract PROPERTIES LABELS "p4;headless" TIMEOUT 120)
        add_executable(editor_scene_gate_test "${root}/tests/editor_scene_gate_test.cpp")
        add_executable(editor_composite_render_test "${root}/tests/editor_composite_render_test.cpp")
        target_link_libraries(editor_composite_render_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render)
        add_test(NAME editor_composite_render COMMAND editor_composite_render_test
            "${root}/tests/data/p4_composite" "${root}/res/assets"
            "${CMAKE_BINARY_DIR}/native-reference")
        # This frozen original reference qualifies the captured native HAL
        # profile. Software rasterizers have independently recorded blend/depth
        # differences; invoke this strict gate explicitly on qualified hardware.
        set_tests_properties(editor_composite_render PROPERTIES LABELS "offscreen;gpu;native-reference" TIMEOUT 120)
        target_link_libraries(editor_scene_gate_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render)
        add_test(NAME editor_scene_gate COMMAND ${Python3_EXECUTABLE}
            "${root}/tests/test_runtime_cpu_scene.py" --passes $<TARGET_FILE:editor_scene_gate_test>
            --assets "${root}/res/assets")
        set_tests_properties(editor_scene_gate PROPERTIES LABELS "p4;headless;oracle" TIMEOUT 120)
        target_link_libraries(qt_editor_performance_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_editor_performance COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_qt_editor_performance.py"
            --test $<TARGET_FILE:qt_editor_performance_test> --assets "${root}/res/assets"
            --report "${CMAKE_BINARY_DIR}/editor-frame-performance.json")
        set_tests_properties(qt_editor_performance PROPERTIES LABELS "p4;offscreen;qt;performance" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_test(NAME qt_editor_gpu_performance COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_qt_editor_performance.py"
            --test $<TARGET_FILE:qt_editor_performance_test> --assets "${root}/res/assets" --gpu
            --report "${CMAKE_BINARY_DIR}/editor-frame-performance-gpu.json")
        set_tests_properties(qt_editor_gpu_performance PROPERTIES LABELS "p4;offscreen;qt;gpu;performance" TIMEOUT 240 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        qt_add_executable(qt_editor_render_test "${root}/tests/qt_editor_render_test.cpp")
        target_link_libraries(qt_editor_render_test PRIVATE mikudancestudio_frontend_qt)
        add_test(NAME qt_editor_render COMMAND ${Python3_EXECUTABLE} "${root}/tests/test_runtime_cpu_scene.py"
            --passes $<TARGET_FILE:qt_editor_render_test> --assets "${root}/res/assets")
        set_tests_properties(qt_editor_render PROPERTIES LABELS "p4;offscreen;qt;gpu" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        if(MIKUDANCESTUDIO_RHI_METAL)
            qt_add_executable(qt_metal_present_test "${root}/tests/qt_metal_present_test.cpp")
            target_link_libraries(qt_metal_present_test PRIVATE mikudancestudio_frontend_qt)
        endif()
        # Fractional/native physical-coordinate profiles. Each process owns its
        # QPA scale; synthetic events never interact with the developer desktop.
        foreach(scale IN ITEMS 1.25 1.5 2)
            string(REPLACE "." "_" profile "${scale}")
            add_test(NAME qt_bone_drag_dpi_${profile} COMMAND ${Python3_EXECUTABLE}
                "${root}/tests/test_runtime_cpu_scene.py"
                --passes $<TARGET_FILE:qt_bone_drag_test> --assets "${root}/res/assets")
            add_test(NAME qt_numeric_dpi_${profile} COMMAND ${Python3_EXECUTABLE}
                "${root}/tests/test_qt_numeric.py"
                --test $<TARGET_FILE:qt_numeric_test> --assets "${root}/res/assets")
            add_test(NAME qt_model_physics_dpi_${profile} COMMAND ${Python3_EXECUTABLE}
                "${root}/tests/test_qt_numeric.py"
                --test $<TARGET_FILE:qt_model_physics_test> --assets "${root}/res/assets"
                --evidence "${root}/tests/reference/model-physics-original.json")
            set_tests_properties(qt_bone_drag_dpi_${profile} qt_numeric_dpi_${profile}
                qt_model_physics_dpi_${profile} PROPERTIES LABELS "p4;offscreen;qt;dpi"
                TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_SCALE_FACTOR=${scale}")
        endforeach()
        qt_add_executable(qt_presentation_test "${root}/tests/qt_presentation_test.cpp")
        target_link_libraries(qt_presentation_test PRIVATE mikudancestudio_frontend_qt)
        if(WIN32)
            if(CMAKE_SIZEOF_VOID_P EQUAL 8)
                add_executable(d3d11_position_transport_probe EXCLUDE_FROM_ALL
                    "${root}/tests/d3d11_position_transport_probe.cpp")
                target_compile_features(d3d11_position_transport_probe PRIVATE cxx_std_17)
                target_compile_definitions(d3d11_position_transport_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
                    MDS_MODEL_VERTEX_SOURCE="${root}/src/render/shaders/model_material_vertex.hlsl")
                target_include_directories(d3d11_position_transport_probe PRIVATE "${root}/include")
                target_link_libraries(d3d11_position_transport_probe PRIVATE d3d11 d3dcompiler dxgi)
                qt_add_executable(qt_control_style_oracle_test EXCLUDE_FROM_ALL "${root}/tests/qt_control_style_oracle_test.cpp"
                    "${root}/res/frontend_qt/windows.manifest")
                target_compile_definitions(qt_control_style_oracle_test PRIVATE
                    MDS_EDIT_LAYOUT_REFERENCE="${root}/tests/reference/original-edit-layouts.json")
                target_link_libraries(qt_control_style_oracle_test PRIVATE mikudancestudio_frontend_qt bcrypt gdi32 user32 comctl32 uxtheme)
                add_executable(editor_scene_gate_oracle_test EXCLUDE_FROM_ALL "${root}/tests/editor_scene_gate_test.cpp")
                target_compile_definitions(editor_scene_gate_oracle_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN MDS_NATIVE_SCENE_GATE_ORACLE)
                target_link_libraries(editor_scene_gate_oracle_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render bcrypt)
                add_executable(editor_numeric_oracle_test EXCLUDE_FROM_ALL "${root}/tests/editor_numeric_oracle_test.cpp")
                target_compile_definitions(editor_numeric_oracle_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
                target_link_libraries(editor_numeric_oracle_test PRIVATE mikudancestudio_editor_core bcrypt)
                add_executable(editor_overlay_oracle_test EXCLUDE_FROM_ALL "${root}/tests/editor_overlay_oracle_test.cpp")
                target_compile_definitions(editor_overlay_oracle_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
                target_link_libraries(editor_overlay_oracle_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render bcrypt)
                add_executable(physics_debug_oracle_test EXCLUDE_FROM_ALL "${root}/tests/physics_debug_oracle_test.cpp")
                target_compile_definitions(physics_debug_oracle_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
                target_link_libraries(physics_debug_oracle_test PRIVATE mikudancestudio_editor_core mikudancestudio_scene_render bcrypt)
            endif()
            qt_add_executable(qt_timeline_gdi_test "${root}/tests/qt_timeline_gdi_test.cpp")
            target_link_libraries(qt_timeline_gdi_test PRIVATE mikudancestudio_frontend_qt gdi32 user32)
            add_test(NAME qt_timeline_gdi COMMAND qt_timeline_gdi_test "${root}/res/assets")
            set_tests_properties(qt_timeline_gdi PROPERTIES LABELS "p4;offscreen;qt;oracle" TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
            # Explicit hidden DX9 oracle; never required on machines without a
            # connected DX9 session. D3DX is loaded only inside this test.
            add_executable(portable_scene_dx9_test "${root}/tests/portable_scene_dx9_test.cpp")
            add_executable(accessory_import_dx9_test EXCLUDE_FROM_ALL "${root}/tests/accessory_import_dx9_test.cpp")
            target_include_directories(accessory_import_dx9_test PRIVATE "${root}/third_party/mmeffect/include")
            target_link_libraries(accessory_import_dx9_test PRIVATE mikudancestudio_model_runtime d3d9)
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
