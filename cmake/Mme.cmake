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
        "${root}/src/render/mme/parameter_defaults.cpp"
        "${root}/src/render/mme/parameter_runtime.cpp"
        "${root}/src/render/mme/parameter_frame.cpp"
        "${root}/src/render/mme/parameter_semantics.cpp"
        "${root}/third_party/mmeffect/src/mme_standard_parameters.cpp"
        "${root}/third_party/mmeffect/src/mme_scene_plan.cpp")
    target_link_libraries(mikudancestudio_mme_runtime PUBLIC mikudancestudio_mme_frontend mikudancestudio_cpu_math)
    target_compile_features(mikudancestudio_mme_runtime PUBLIC cxx_std_17)
    add_executable(mds_effect_cli "${root}/src/tools/effect_cli.cpp")
    target_link_libraries(mds_effect_cli PRIVATE mikudancestudio_mme_runtime)
    set(graph "{")
    foreach(name IN ITEMS mds_effect_cli mikudancestudio_mme_runtime mikudancestudio_cpu_math mikudancestudio_mme_frontend mikudancestudio_tool_process mikudancestudio_formats)
        get_target_property(sources ${name} SOURCES)
        get_target_property(directory ${name} SOURCE_DIR)
        get_target_property(links ${name} LINK_LIBRARIES)
        if(NOT links)
            set(links "")
        endif()
        set(files "")
        foreach(source IN LISTS sources)
            get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${directory}")
            list(APPEND files "${source}")
        endforeach()
        string(REPLACE ";" "\",\"" files "${files}")
        string(REPLACE ";" "\",\"" links "${links}")
        if(NOT graph STREQUAL "{")
            string(APPEND graph ",")
        endif()
        string(APPEND graph "\"${name}\":{\"sources\":[\"${files}\"],\"links\":[\"${links}\"]}")
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/mme_frontend_targets.json" CONTENT "${graph}}\n")
endfunction()
