# A redistributable last-resort CJK face; installed original fonts retain priority.
set(MDS_EDITOR_FONT_FILE "" CACHE FILEPATH "Pinned Noto Sans CJK 2.004 TTC (optional offline input)")
set(mds_font_sha256 "b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a")
if(NOT MDS_EDITOR_FONT_FILE)
    set(MDS_EDITOR_FONT_FILE "${CMAKE_BINARY_DIR}/editor-fonts/NotoSansCJK-Regular.ttc")
    if(NOT EXISTS "${MDS_EDITOR_FONT_FILE}")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        execute_process(COMMAND "${Python3_EXECUTABLE}" "${root}/tools/fetch_editor_fonts.py"
            --output "${CMAKE_BINARY_DIR}/editor-fonts" RESULT_VARIABLE mds_font_error TIMEOUT 90)
        if(NOT mds_font_error EQUAL 0)
            message(FATAL_ERROR "Noto CJK download failed. Set MDS_EDITOR_FONT_FILE for offline builds.")
        endif()
    endif()
endif()
file(SHA256 "${MDS_EDITOR_FONT_FILE}" mds_font_actual)
if(NOT mds_font_actual STREQUAL mds_font_sha256)
    message(FATAL_ERROR "MDS_EDITOR_FONT_FILE must be the pinned unmodified Noto Sans CJK 2.004 TTC")
endif()
add_custom_target(mds_editor_fonts
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mikudancestudio_frontend_qt>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MDS_EDITOR_FONT_FILE}"
        "$<TARGET_FILE_DIR:mikudancestudio_frontend_qt>/fonts/NotoSansCJK-Regular.ttc"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MikuDanceStudioNG>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MDS_EDITOR_FONT_FILE}"
        "$<TARGET_FILE_DIR:MikuDanceStudioNG>/fonts/NotoSansCJK-Regular.ttc"
    VERBATIM)
add_dependencies(mikudancestudio_frontend_qt mds_editor_fonts)
