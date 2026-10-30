# Build and package a real Qt plugin in one target. Resource paths in the
# manifest are relative to plugin.json; the packer captures ABI metadata.
function(corig_add_plugin target)
    cmake_parse_arguments(PLUGIN "" "MANIFEST" "SOURCES" ${ARGN})
    if(NOT PLUGIN_MANIFEST OR NOT PLUGIN_SOURCES)
        message(FATAL_ERROR "corig_add_plugin requires MANIFEST and SOURCES")
    endif()
    get_filename_component(manifest "${PLUGIN_MANIFEST}" ABSOLUTE)
    add_library(${target} MODULE ${PLUGIN_SOURCES})
    set_target_properties(${target} PROPERTIES AUTOMOC ON CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(${target} PRIVATE CoRig::Sdk Qt6::Core)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive-)
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    file(READ "${manifest}" manifest_json)
    string(JSON resource_count ERROR_VARIABLE no_resources LENGTH "${manifest_json}" files)
    set(resources "${manifest}")
    if(NOT no_resources AND resource_count GREATER 0)
        math(EXPR last_resource "${resource_count} - 1")
        get_filename_component(manifest_dir "${manifest}" DIRECTORY)
        foreach(index RANGE 0 ${last_resource})
            string(JSON resource GET "${manifest_json}" files ${index})
            list(APPEND resources "${manifest_dir}/${resource}")
        endforeach()
    endif()
    # Qt's binary directory supplies the developer runtime only to this build
    # step. Nothing changes the user's PATH or selected Python environment.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env --modify "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>" --
            "${CoRigSdk_PACKER}" pack "${manifest}" "$<TARGET_FILE:${target}>" "${CMAKE_CURRENT_BINARY_DIR}/${target}.crpl"
        VERBATIM)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${resources}")
endfunction()
