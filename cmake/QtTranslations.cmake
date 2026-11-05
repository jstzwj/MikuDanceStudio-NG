# ID-based Qt Linguist catalogs preserve the original numeric/control IDs.
file(GLOB mds_ts_files CONFIGURE_DEPENDS "${root}/translations/mikudancestudio_*.ts")
set_source_files_properties(${mds_ts_files} PROPERTIES
    OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/translations")
qt_add_lrelease(mikudancestudio_frontend_qt TS_FILES ${mds_ts_files}
    QM_FILES_OUTPUT_VARIABLE mds_qm_files OPTIONS -idbased -nounfinished)
qt_add_resources(mikudancestudio_frontend_qt editor_translations
    PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}/translations" FILES ${mds_qm_files})
get_filename_component(mds_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
set(mds_qt_base_translations)
foreach(mds_ts IN LISTS mds_ts_files)
    get_filename_component(mds_locale "${mds_ts}" NAME_WE)
    string(REPLACE "mikudancestudio_" "" mds_locale "${mds_locale}")
    if(NOT mds_locale STREQUAL "en")
        set(mds_qt_base "${mds_qt_prefix}/translations/qtbase_${mds_locale}.qm")
        if(NOT EXISTS "${mds_qt_base}")
            message(FATAL_ERROR "Missing Qt standard-dialog translation: ${mds_qt_base}")
        endif()
        list(APPEND mds_qt_base_translations "${mds_qt_base}")
    endif()
endforeach()
qt_add_resources(mikudancestudio_frontend_qt qt_base_translations
    PREFIX "/i18n/qt" BASE "${mds_qt_prefix}/translations" FILES ${mds_qt_base_translations})
# Dynamic text IDs are declared with QT_TRID_NOOP and English //% source text.
# Refresh the declaration table before running Qt's native extraction tool.
add_custom_target(update_translations
    COMMAND "${Python3_EXECUTABLE}" "${root}/scripts/translation_catalog.py" --generate
    COMMAND Qt6::lupdate "${root}/translations/messages.hpp" -locations none -no-obsolete
        -ts ${mds_ts_files}
    COMMENT "Update Qt Linguist TS catalogs from the stable editor text IDs"
    VERBATIM)
