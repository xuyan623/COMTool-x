cmake_minimum_required(VERSION 3.25)
project(CoRig VERSION 0.4.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(CORIG_BUILD_APP "Build the Qt Quick desktop application" ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Core CorePrivate Test Gui Qml Quick QuickControls2 Network SerialPort Svg Core5Compat)
qt_standard_project_setup(REQUIRES 6.12)

add_library(corig_core SHARED
    src/core/value.cpp src/core/value.h
    src/core/catalog.cpp src/core/catalog.h
    src/core/project.cpp src/core/project.h
    src/core/recording.cpp src/core/recording.h
    src/core/plugin_package.cpp src/core/plugin_package.h
    src/core/paths.cpp src/core/paths.h
    src/core/resource_policy.cpp src/core/resource_policy.h
    src/core/plugin_script.cpp src/core/plugin_script.h
)
set_target_properties(corig_core PROPERTIES OUTPUT_NAME CoRigSdk WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_compile_definitions(corig_core PRIVATE CORIG_CORE_BUILD)
target_include_directories(corig_core PUBLIC src)
target_link_libraries(corig_core PUBLIC Qt6::Core PRIVATE Qt6::CorePrivate Qt6::Qml)
qt_add_executable(CoRig-pack src/app/pack.cpp)
target_link_libraries(CoRig-pack PRIVATE corig_core)
include(CMakePackageConfigHelpers)
configure_package_config_file(sdk/cmake/CoRigSdkConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/sdk-config/CoRigSdkConfig.cmake"
    INSTALL_DESTINATION lib/cmake/CoRigSdk)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/sdk-config/CoRigSdkConfigVersion.cmake"
    VERSION 2.0.0 COMPATIBILITY SameMajorVersion)
set(SDK_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/sdk/CoRigSdk")
add_custom_target(corig_sdk ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SDK_OUTPUT}/include/core" "${SDK_OUTPUT}/bin" "${SDK_OUTPUT}/lib/cmake/CoRigSdk"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/sdk/include" "${SDK_OUTPUT}/include"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/sdk/examples" "${SDK_OUTPUT}/examples"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SDK_OUTPUT}/docs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/sdk/README.md" "${SDK_OUTPUT}/README.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/docs/PLUGIN_SDK.md" "${SDK_OUTPUT}/docs/PLUGIN_SDK.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:corig_core>" "$<TARGET_FILE:CoRig-pack>" "${SDK_OUTPUT}/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_LINKER_FILE:corig_core>" "${SDK_OUTPUT}/lib"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/sdk-config/CoRigSdkConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/sdk-config/CoRigSdkConfigVersion.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/CoRigPlugin.cmake" "${SDK_OUTPUT}/lib/cmake/CoRigSdk"
    DEPENDS corig_core CoRig-pack VERBATIM)
foreach(header export value catalog plugin_package node native_plugin resource_policy plugin_script)
    add_custom_command(TARGET corig_sdk POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core/${header}.h" "${SDK_OUTPUT}/include/core/${header}.h" VERBATIM)
endforeach()
    target_sources(corig_core PRIVATE
        src/core/node.cpp src/core/node.h src/core/engine.cpp src/core/engine.h
        src/core/series.cpp src/core/series.h src/core/mesh.cpp src/core/mesh.h
        src/core/python_host.cpp src/core/python_host.h
        src/core/python_node.cpp src/core/native_plugin.cpp src/core/native_plugin.h
    )
    target_link_libraries(corig_core PUBLIC Qt6::Gui Qt6::Qml Qt6::Core5Compat)
    qt_add_resources(corig_core python_worker PREFIX "/" BASE "python" FILES python/worker.py)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CRYPTO_BACKEND OpenSSL CACHE STRING "" FORCE)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    if(WIN32 AND NOT OPENSSL_ROOT_DIR)
        set(OPENSSL_ROOT_DIR "D:/Software/DevTools/OpenSSL/3.5.9/msvc2022_64")
    endif()
    add_subdirectory(third_party/libssh2-1.11.1 EXCLUDE_FROM_ALL)
    add_library(corig_io STATIC src/plugins/io_hub.cpp src/plugins/io_hub.h src/plugins/ssh_session.cpp src/plugins/ssh_session.h)
    target_link_libraries(corig_io PUBLIC corig_core Qt6::Network Qt6::SerialPort PRIVATE libssh2_static ws2_32 crypt32 bcrypt)
    add_library(corig_official_impl STATIC
        src/plugins/math_node.cpp src/plugins/math_node.h
        src/plugins/logic_node.cpp src/plugins/logic_node.h
        src/plugins/timer_node.cpp src/plugins/timer_node.h
        src/plugins/recording_node.cpp src/plugins/recording_node.h
        src/plugins/data_node.cpp src/plugins/data_node.h
        src/plugins/protocol_node.cpp src/plugins/protocol_node.h
        src/plugins/instrument_node.cpp src/plugins/instrument_node.h
        src/plugins/terminal_screen.cpp src/plugins/terminal_screen.h
    )
    target_link_libraries(corig_official_impl PUBLIC corig_core Qt6::Core5Compat Qt6::Gui Qt6::Qml)
    add_library(corig_official MODULE src/plugins/official_plugin.cpp src/plugins/official_catalog.cpp src/plugins/official_catalog.h)
    target_link_libraries(corig_official PRIVATE corig_official_impl corig_io)
    set(OFFICIAL_PACKAGE "${CMAKE_CURRENT_BINARY_DIR}/plugins/.official.stamp")
    file(GLOB OFFICIAL_QML_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/qml/*View.qml")
    add_custom_command(OUTPUT "${OFFICIAL_PACKAGE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/plugins"
        COMMAND $<TARGET_FILE:CoRig-pack> official "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/catalog.json" $<TARGET_FILE:corig_official> "${CMAKE_CURRENT_SOURCE_DIR}/src/qml" "${CMAKE_CURRENT_BINARY_DIR}/plugins"
        COMMAND ${CMAKE_COMMAND} -E touch "${OFFICIAL_PACKAGE}"
        DEPENDS CoRig-pack corig_official src/plugins/catalog.json ${OFFICIAL_QML_FILES}
        VERBATIM)
    add_custom_target(corig_official_package ALL DEPENDS "${OFFICIAL_PACKAGE}")
    add_library(corig_design STATIC src/quick/graph_geometry.cpp src/quick/graph_geometry.h src/app/model_commands.cpp src/app/model_commands.h src/app/editor_commands.cpp)
    target_link_libraries(corig_design PUBLIC corig_core Qt6::Gui)
if(CORIG_BUILD_APP)
    add_library(corig_app STATIC src/app/controller.cpp src/app/controller_commands.cpp src/app/ui_commands.cpp src/app/controller.h
        src/app/property_session.cpp src/app/property_session.h
        src/app/plugin_store.cpp src/app/plugin_store.h
        src/app/updates.cpp src/app/updates.h
        src/app/api_server.cpp src/app/api_server.h src/app/workspaces.cpp src/app/workspaces.h)
    target_link_libraries(corig_app PUBLIC corig_design Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
    add_library(corig_quick STATIC src/quick/renderers.cpp src/quick/renderers.h src/quick/icon_provider.cpp src/quick/icon_provider.h)
    target_link_libraries(corig_quick PUBLIC corig_design Qt6::Quick Qt6::Svg)
    qt_add_executable(CoRig WIN32 src/app/main.cpp)
    if(WIN32)
        target_sources(CoRig PRIVATE src/assets/application.rc)
        target_include_directories(CoRig PRIVATE src/assets)
    endif()
    qt_add_resources(CoRig application_assets PREFIX "/" BASE "src/assets" FILES src/assets/icon.svg)
    target_link_libraries(CoRig PRIVATE corig_app corig_quick Qt6::QuickControls2)
    file(GLOB CORIG_QML_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/qml/*.qml" "${CMAKE_CURRENT_SOURCE_DIR}/src/qml/*.js")
    foreach(view PlotView AttitudeView ConsoleView ConnectionView ControlView ValueView RecorderView PlaybackView TerminalView)
        list(REMOVE_ITEM CORIG_QML_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/qml/${view}.qml")
    endforeach()
    foreach(qml_file IN LISTS CORIG_QML_FILES)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    set_source_files_properties(src/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(CoRig URI CoRig VERSION 1.0 RESOURCE_PREFIX "/qt/qml" QML_FILES ${CORIG_QML_FILES})
    qt_add_executable(CoRig-cli src/app/cli.cpp)
    target_link_libraries(CoRig-cli PRIVATE corig_core Qt6::Network)
endif()
if(MSVC)
    target_compile_options(corig_core PUBLIC /utf-8 /W4 /permissive-)
    target_compile_definitions(corig_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

enable_testing()
if(CORIG_BUILD_APP)
    qt_add_executable(corig_plugin_test tests/native/plugin_test.cpp)
    target_link_libraries(corig_plugin_test PRIVATE corig_app Qt6::Test Qt6::CorePrivate)
    add_test(NAME plugins COMMAND corig_plugin_test)
    qt_add_executable(corig_editor_test tests/native/editor_test.cpp)
    target_link_libraries(corig_editor_test PRIVATE corig_design Qt6::Test)
    add_test(NAME editor COMMAND corig_editor_test)
    qt_add_executable(corig_workbench_ui_test tests/native/workbench_ui_test.cpp)
    target_link_libraries(corig_workbench_ui_test PRIVATE corig_app corig_quick Qt6::Test Qt6::QuickControls2)
    target_compile_definitions(corig_workbench_ui_test PRIVATE CORIG_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(corig_workbench_ui_test corig_editor_test PROPERTIES WIN32_EXECUTABLE FALSE)
    qt_add_resources(corig_workbench_ui_test workbench_qml PREFIX "/qt/qml/CoRig" BASE "src/qml" FILES ${CORIG_QML_FILES})
    qt_add_resources(corig_workbench_ui_test workbench_module PREFIX "/qt/qml/CoRig" BASE "tests/native" FILES tests/native/qmldir)
    add_test(NAME workbench_ui COMMAND corig_workbench_ui_test)
    set_tests_properties(workbench_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
qt_add_executable(corig_core_test tests/native/core_test.cpp)
target_link_libraries(corig_core_test PRIVATE corig_core Qt6::Test)
add_test(NAME core COMMAND corig_core_test)
if(CORIG_BUILD_APP)
    qt_add_executable(corig_execution_test tests/native/execution_test.cpp)
    target_link_libraries(corig_execution_test PRIVATE corig_core corig_official_impl Qt6::Test)
    add_test(NAME execution COMMAND corig_execution_test)
    qt_add_executable(corig_control_test tests/native/control_test.cpp)
    target_link_libraries(corig_control_test PRIVATE corig_core Qt6::Test)
    add_test(NAME control COMMAND corig_control_test)
    qt_add_executable(corig_recording_test tests/native/recording_test.cpp)
    target_link_libraries(corig_recording_test PRIVATE corig_design Qt6::Test)
    add_test(NAME recording COMMAND corig_recording_test)
    qt_add_executable(corig_io_test tests/native/io_test.cpp)
    target_link_libraries(corig_io_test PRIVATE corig_io Qt6::Test)
    add_test(NAME io COMMAND corig_io_test)
endif()

if(CORIG_BUILD_APP)
    foreach(target CoRig corig_plugin_test corig_editor_test corig_workbench_ui_test corig_execution_test corig_control_test corig_recording_test corig_io_test)
        add_dependencies(${target} corig_official_package)
    endforeach()
endif()

add_dependencies(corig_core_test corig_official_package)
