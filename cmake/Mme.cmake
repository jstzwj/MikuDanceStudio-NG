# The FX frontend and CPU execution plans are production libraries. Neither
# depends on a native effect, a renderer, a window system or the old host.
function(mds_add_mme)
    if(TARGET mikudancestudio_mme_frontend)
        return()
    endif()
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_library(mikudancestudio_tool_process STATIC "${root}/src/platform/tool_process.cpp")
    target_include_directories(mikudancestudio_tool_process PUBLIC "${root}/include")
    target_compile_features(mikudancestudio_tool_process PUBLIC cxx_std_17)
    add_library(mikudancestudio_mme_frontend STATIC
        "${root}/third_party/mmeffect/src/mme_legacy_source.cpp"
        "${root}/src/render/mme/effect_source.cpp")
    target_include_directories(mikudancestudio_mme_frontend PUBLIC
        "${root}/third_party/mmeffect/include" "${root}/include")
    target_compile_features(mikudancestudio_mme_frontend PUBLIC cxx_std_17)
    target_link_libraries(mikudancestudio_mme_frontend PRIVATE
        mikudancestudio_tool_process mikudancestudio_formats)
    add_library(mikudancestudio_mme_runtime STATIC
        "${root}/third_party/mmeffect/src/mme_standard_parameters.cpp"
        "${root}/third_party/mmeffect/src/mme_scene_plan.cpp")
    target_link_libraries(mikudancestudio_mme_runtime PUBLIC mikudancestudio_mme_frontend)
    target_compile_features(mikudancestudio_mme_runtime PUBLIC cxx_std_17)
endfunction()
