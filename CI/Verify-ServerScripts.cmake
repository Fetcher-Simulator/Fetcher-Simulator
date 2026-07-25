cmake_minimum_required(VERSION 3.20)

foreach(required_variable IN ITEMS SERVER_SCRIPTS_DIR SERVER_SCRIPT_MANIFEST LUA_LIBS_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be provided")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${SERVER_SCRIPTS_DIR}")
    message(FATAL_ERROR "Server scripts directory does not exist: ${SERVER_SCRIPTS_DIR}")
endif()
if(NOT EXISTS "${SERVER_SCRIPT_MANIFEST}")
    message(FATAL_ERROR "Server script manifest does not exist: ${SERVER_SCRIPT_MANIFEST}")
endif()
if(NOT IS_DIRECTORY "${LUA_LIBS_DIR}")
    message(FATAL_ERROR "Server Lua libraries directory does not exist: ${LUA_LIBS_DIR}")
endif()

file(STRINGS "${SERVER_SCRIPT_MANIFEST}" expected_files)
list(FILTER expected_files EXCLUDE REGEX "^[ \t]*(#|$)")
set(normalized_expected_files)
foreach(file_name IN LISTS expected_files)
    string(STRIP "${file_name}" file_name)
    if(NOT file_name MATCHES "^[A-Za-z0-9_.-]+$")
        message(FATAL_ERROR "Invalid entry in server script manifest: ${file_name}")
    endif()
    list(APPEND normalized_expected_files "${file_name}")
endforeach()
set(expected_files "${normalized_expected_files}")

set(unique_expected_files "${expected_files}")
list(REMOVE_DUPLICATES unique_expected_files)
list(LENGTH expected_files expected_count)
list(LENGTH unique_expected_files unique_expected_count)
if(NOT expected_count EQUAL unique_expected_count)
    message(FATAL_ERROR "Server script manifest contains duplicate entries")
endif()

file(GLOB_RECURSE discovered_paths
    LIST_DIRECTORIES false
    RELATIVE "${SERVER_SCRIPTS_DIR}"
    "${SERVER_SCRIPTS_DIR}/*")
set(actual_files)
foreach(file_name IN LISTS discovered_paths)
    string(REPLACE "\\" "/" file_name "${file_name}")
    list(APPEND actual_files "${file_name}")
endforeach()

list(SORT expected_files)
list(SORT actual_files)
if(NOT "${actual_files}" STREQUAL "${expected_files}")
    set(missing_files "${expected_files}")
    foreach(file_name IN LISTS actual_files)
        list(REMOVE_ITEM missing_files "${file_name}")
    endforeach()

    set(unexpected_files "${actual_files}")
    foreach(file_name IN LISTS expected_files)
        list(REMOVE_ITEM unexpected_files "${file_name}")
    endforeach()

    message(FATAL_ERROR
        "Server Lua payload does not match the release manifest.\n"
        "Missing: ${missing_files}\n"
        "Unexpected: ${unexpected_files}")
endif()

if(NOT EXISTS "${LUA_LIBS_DIR}/util.lua")
    message(FATAL_ERROR "Server Lua internal library is missing: ${LUA_LIBS_DIR}/util.lua")
endif()

file(GLOB backup_files "${SERVER_SCRIPTS_DIR}/*.bak")
if(backup_files)
    message(FATAL_ERROR "Backup files must not be packaged: ${backup_files}")
endif()
if(EXISTS "${SERVER_SCRIPTS_DIR}/test_callbacks.lua")
    message(FATAL_ERROR "test_callbacks.lua must not be packaged")
endif()

list(LENGTH actual_files actual_count)
message(STATUS "Verified ${actual_count} dedicated server Lua files and util.lua")
