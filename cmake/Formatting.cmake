find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND NOT TARGET format)
    get_filename_component(mds_format_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    add_custom_target(format
        COMMAND "${Python3_EXECUTABLE}" "${mds_format_root}/scripts/format_sources.py"
        WORKING_DIRECTORY "${mds_format_root}"
        COMMENT "Format first-party C/C++/Objective-C++ sources" VERBATIM)
    add_custom_target(format-check
        COMMAND "${Python3_EXECUTABLE}" "${mds_format_root}/scripts/format_sources.py" --check
        WORKING_DIRECTORY "${mds_format_root}"
        COMMENT "Check first-party clang-format style" VERBATIM)
endif()
