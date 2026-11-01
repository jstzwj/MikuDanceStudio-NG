find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Multimedia Test)
if(APPLE)
    enable_language(OBJCXX)
endif()
function(mds_add_qt_editor)
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    qt_add_library(mikudancestudio_frontend_qt STATIC
        "${root}/src/frontend_qt/language.cpp"
        "${root}/src/frontend_qt/timeline.cpp"
        "${root}/src/frontend_qt/editor_window.cpp"
        "${root}/src/frontend_qt/viewport.cpp"
        "${root}/src/frontend_qt/surface.cpp")
    target_sources(mikudancestudio_frontend_qt PRIVATE "${root}/src/frontend_qt/audio.cpp" "${root}/src/frontend_qt/export.cpp")
    target_include_directories(mikudancestudio_frontend_qt PUBLIC "${root}/src/frontend_qt")
    target_link_libraries(mikudancestudio_frontend_qt PUBLIC mikudancestudio_editor_core mikudancestudio_scene_render Qt6::Widgets Qt6::Multimedia)
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
    endif()
    qt_add_resources(mikudancestudio_frontend_qt editor_translations
        PREFIX "/i18n" BASE "${root}/res/i18n"
        FILES "${root}/res/i18n/en.json" "${root}/res/i18n/ja.json" "${root}/res/i18n/zh_CN.json")
    qt_add_executable(MikuDanceStudioNG "${root}/src/frontend_qt/main.cpp")
    target_link_libraries(MikuDanceStudioNG PRIVATE mikudancestudio_frontend_qt)
    add_custom_command(TARGET MikuDanceStudioNG POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${root}/res/assets" "$<TARGET_FILE_DIR:MikuDanceStudioNG>/assets")
    if(BUILD_TESTING)
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
        # Native platform tests are invoked explicitly in isolated CI displays;
        # the normal p4 suite must remain safe for a developer's desktop.
    endif()
endfunction()
