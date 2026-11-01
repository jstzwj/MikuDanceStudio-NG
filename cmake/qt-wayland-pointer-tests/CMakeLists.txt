find_package(Qt6 6.8 REQUIRED COMPONENTS WaylandCompositor)
pkg_check_modules(MDS_WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
qt_add_executable(qt_wayland_pointer_test "${root}/tests/qt_wayland_pointer_test.cpp")
target_link_libraries(qt_wayland_pointer_test PRIVATE mikudancestudio_frontend_qt
    Qt6::WaylandCompositor Qt6::Test PkgConfig::MDS_WAYLAND_SERVER)
qt_generate_wayland_protocol_server_sources(qt_wayland_pointer_test PRIVATE_CODE FILES
    "${MDS_WAYLAND_PROTOCOLS}/unstable/relative-pointer/relative-pointer-unstable-v1.xml"
    "${MDS_WAYLAND_PROTOCOLS}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")
foreach(scale IN ITEMS 1 1.5 2)
    add_test(NAME qt_wayland_pointer_${scale} COMMAND qt_wayland_pointer_test normal ${scale})
    set_tests_properties(qt_wayland_pointer_${scale} PROPERTIES LABELS "p4;offscreen;qt;wayland"
        TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
foreach(mode IN ITEMS missing denied)
    add_test(NAME qt_wayland_pointer_${mode} COMMAND qt_wayland_pointer_test ${mode} 1)
    set_tests_properties(qt_wayland_pointer_${mode} PROPERTIES LABELS "p4;offscreen;qt;wayland"
        TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
