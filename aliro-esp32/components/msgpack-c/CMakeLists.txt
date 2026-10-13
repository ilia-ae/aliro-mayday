# msgpack-c wrapper for ESP-IDF — vendored from rednblkx/HomeKey-ESP32.
#
# msgpack-c upstream uses CMake's add_library + install rules. We override
# the `export()` command and disable install rules so the upstream subdir
# can be slotted into the ESP-IDF component build without polluting the
# install tree, then expose the headers + library via this thin component.
set(BUILD_SHARED_LIBS OFF)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "Disable msgpack examples")
set(MSGPACK_BUILD_TESTS OFF CACHE BOOL "Disable msgpack tests")
set(CMAKE_SKIP_INSTALL_RULES ON)
function(export)
    # No-op shim so upstream `export(TARGETS …)` calls don't fail.
endfunction()
idf_component_register(INCLUDE_DIRS "upstream/include")
add_subdirectory(upstream)
target_include_directories(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/upstream/include")
target_include_directories(${COMPONENT_LIB} INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/upstream/include/msgpack")
target_link_libraries(${COMPONENT_LIB} INTERFACE msgpack-c)
