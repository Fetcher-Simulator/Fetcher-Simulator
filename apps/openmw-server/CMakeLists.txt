set(OPENMW_SERVER_SOURCES
    AdminHttpServer.cpp
    main.cpp
    Server.cpp
    LuaServerContext.cpp
    MasterServerClient.cpp
    PlayerDatabase.cpp
    DynamicRecordService.cpp
    AlchemyService.cpp
    EnchantingService.cpp
    ServerContentRegistry.cpp
    ServerLuaRecordParser.cpp
    ServerLuaPackageRegistry.cpp
    bindings/PlayerBindings.cpp
    bindings/ServerBindings.cpp
    bindings/TypesStubBindings.cpp
    ${CMAKE_SOURCE_DIR}/extern/bcrypt/bcrypt_impl.cpp
)

set(OPENMW_SERVER_HEADERS
    AdminHttpServer.hpp
    Server.hpp
    LuaServerContext.hpp
    MpEventQueue.hpp
    OutboundQueue.hpp
    MasterServerClient.hpp
    PlayerDatabase.hpp
    DynamicRecordService.hpp
    AlchemyService.hpp
    EnchantingService.hpp
    ServerContentRegistry.hpp
    ServerLuaRecordParser.hpp
    ServerLuaPackageRegistry.hpp
    bindings/PlayerBindings.hpp
    bindings/ServerBindings.hpp
    bindings/TypesStubBindings.hpp
)

source_group("apps\\openmw-server" FILES
    ${OPENMW_SERVER_SOURCES}
    ${OPENMW_SERVER_HEADERS}
)

# One manifest is the source of truth for build-tree copies, installed packages,
# and CI verification. This avoids copying development harnesses or local
# backup files merely because they exist in the scripts directory.
set(OPENMW_SERVER_SCRIPTS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
set(OPENMW_SERVER_RELEASE_MANIFEST "${OPENMW_SERVER_SCRIPTS_SOURCE_DIR}/release-files.txt")
file(STRINGS "${OPENMW_SERVER_RELEASE_MANIFEST}" OPENMW_SERVER_RELEASE_FILE_NAMES)
list(FILTER OPENMW_SERVER_RELEASE_FILE_NAMES EXCLUDE REGEX "^[ \t]*(#|$)")

set(OPENMW_SERVER_RELEASE_FILES)
set(OPENMW_SERVER_NORMALIZED_RELEASE_FILE_NAMES)
foreach(file_name IN LISTS OPENMW_SERVER_RELEASE_FILE_NAMES)
    string(STRIP "${file_name}" file_name)
    if(NOT file_name MATCHES "^[A-Za-z0-9_.-]+$")
        message(FATAL_ERROR "Invalid dedicated server release file name: ${file_name}")
    endif()

    set(source_file "${OPENMW_SERVER_SCRIPTS_SOURCE_DIR}/${file_name}")
    if(NOT EXISTS "${source_file}")
        message(FATAL_ERROR "Dedicated server release file is missing: ${source_file}")
    endif()

    list(APPEND OPENMW_SERVER_NORMALIZED_RELEASE_FILE_NAMES "${file_name}")
    list(APPEND OPENMW_SERVER_RELEASE_FILES "${source_file}")
endforeach()
set(OPENMW_SERVER_RELEASE_FILE_NAMES "${OPENMW_SERVER_NORMALIZED_RELEASE_FILE_NAMES}")

set(OPENMW_SERVER_UNIQUE_RELEASE_FILE_NAMES "${OPENMW_SERVER_RELEASE_FILE_NAMES}")
list(REMOVE_DUPLICATES OPENMW_SERVER_UNIQUE_RELEASE_FILE_NAMES)
list(LENGTH OPENMW_SERVER_RELEASE_FILE_NAMES OPENMW_SERVER_RELEASE_FILE_COUNT)
list(LENGTH OPENMW_SERVER_UNIQUE_RELEASE_FILE_NAMES OPENMW_SERVER_UNIQUE_RELEASE_FILE_COUNT)
if(NOT OPENMW_SERVER_RELEASE_FILE_COUNT EQUAL OPENMW_SERVER_UNIQUE_RELEASE_FILE_COUNT)
    message(FATAL_ERROR "Dedicated server release manifest contains duplicate file names")
endif()

# Every source Lua file must be explicitly classified as release content or a
# development-only harness. New modules cannot silently disappear from packages.
set(OPENMW_SERVER_DEVELOPMENT_ONLY_FILE_NAMES
    test_callbacks.lua
)
file(GLOB OPENMW_SERVER_SOURCE_LUA_FILE_NAMES
    CONFIGURE_DEPENDS
    LIST_DIRECTORIES false
    RELATIVE "${OPENMW_SERVER_SCRIPTS_SOURCE_DIR}"
    "${OPENMW_SERVER_SCRIPTS_SOURCE_DIR}/*.lua")

set(OPENMW_SERVER_CLASSIFIED_LUA_FILE_NAMES ${OPENMW_SERVER_DEVELOPMENT_ONLY_FILE_NAMES})
foreach(file_name IN LISTS OPENMW_SERVER_RELEASE_FILE_NAMES)
    if(file_name MATCHES "\\.lua$")
        list(APPEND OPENMW_SERVER_CLASSIFIED_LUA_FILE_NAMES "${file_name}")
    endif()
endforeach()

foreach(file_name IN LISTS OPENMW_SERVER_DEVELOPMENT_ONLY_FILE_NAMES)
    list(FIND OPENMW_SERVER_RELEASE_FILE_NAMES "${file_name}" release_index)
    if(NOT release_index EQUAL -1)
        message(FATAL_ERROR "Development-only server Lua file is listed for release: ${file_name}")
    endif()
    if(NOT EXISTS "${OPENMW_SERVER_SCRIPTS_SOURCE_DIR}/${file_name}")
        message(FATAL_ERROR "Classified development-only server Lua file is missing: ${file_name}")
    endif()
endforeach()

foreach(file_name IN LISTS OPENMW_SERVER_SOURCE_LUA_FILE_NAMES)
    list(FIND OPENMW_SERVER_CLASSIFIED_LUA_FILE_NAMES "${file_name}" classified_index)
    if(classified_index EQUAL -1)
        message(FATAL_ERROR
            "Server Lua file is not classified in release-files.txt or the development-only list: ${file_name}")
    endif()
endforeach()

add_executable(openmw-server
    ${OPENMW_SERVER_SOURCES}
)

target_include_directories(openmw-server PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${GNS_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/extern/sol3
    ${CMAKE_SOURCE_DIR}/extern/httplib
    ${CMAKE_SOURCE_DIR}/extern/GameNetworkingSockets/src/common  # crypto_25519.h, keypair.h
    ${CMAKE_SOURCE_DIR}/extern/GameNetworkingSockets/src/public  # tier0/*, minbase/*
    ${LUA_INCLUDE_DIR}
)

if(WIN32)
    # Portable and NSIS packages place runtime data beside the executables.
    set(OPENMW_SERVER_INSTALL_SCRIPTS_DESTINATION "server-scripts")
    set(OPENMW_SERVER_INSTALL_LUA_LIBS_DESTINATION "resources/lua_libs")
elseif(UNIX AND NOT APPLE)
    set(OPENMW_SERVER_INSTALL_SCRIPTS_DESTINATION "${DATADIR}/server-scripts")
    set(OPENMW_SERVER_INSTALL_LUA_LIBS_DESTINATION "${DATADIR}/lua_libs")
endif()

target_compile_definitions(openmw-server PRIVATE
    CPPHTTPLIB_OPENSSL_SUPPORT
    VALVE_CRYPTO_ENABLE_25519   # exposes CCrypto::GenerateSigningKeyPair, CECSigning* etc.
    OPENMW_SERVER_SOURCE_SCRIPTS_DIR="${OPENMW_SERVER_SCRIPTS_SOURCE_DIR}"
    OPENMW_SERVER_SOURCE_LUA_LIBS_DIR="${CMAKE_SOURCE_DIR}/components/lua"
)

if(DEFINED OPENMW_SERVER_INSTALL_SCRIPTS_DESTINATION)
    target_compile_definitions(openmw-server PRIVATE
        OPENMW_SERVER_INSTALL_SCRIPTS_DIR="${OPENMW_SERVER_INSTALL_SCRIPTS_DESTINATION}"
        OPENMW_SERVER_INSTALL_LUA_LIBS_DIR="${OPENMW_SERVER_INSTALL_LUA_LIBS_DESTINATION}"
    )
endif()

target_link_libraries(openmw-server
    openmw-lib
    components
    GameNetworkingSockets::GameNetworkingSockets_s
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite3::SQLite3
    ${LUA_LIBRARIES}
)

if (UNIX)
    target_link_libraries(openmw-server crypt)
endif()

target_compile_features(openmw-server PRIVATE cxx_std_20)

install(TARGETS openmw-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(DEFINED OPENMW_SERVER_INSTALL_SCRIPTS_DESTINATION)
    install(FILES ${OPENMW_SERVER_RELEASE_FILES}
        DESTINATION "${OPENMW_SERVER_INSTALL_SCRIPTS_DESTINATION}")
    install(FILES "${CMAKE_SOURCE_DIR}/components/lua/util.lua"
        DESTINATION "${OPENMW_SERVER_INSTALL_LUA_LIBS_DESTINATION}")
endif()

# Keep build-tree execution equivalent to an installed portable package, but
# copy only the release manifest instead of every source-directory file.
add_custom_command(TARGET openmw-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_FILE_DIR:openmw-server>/server-scripts"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:openmw-server>/server-scripts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENMW_SERVER_RELEASE_FILES}
        "$<TARGET_FILE_DIR:openmw-server>/server-scripts"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:openmw-server>/resources/lua_libs"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/components/lua/util.lua"
        "$<TARGET_FILE_DIR:openmw-server>/resources/lua_libs/util.lua"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/files/server/Build-OpenMWServerAuthority.py"
        "$<TARGET_FILE_DIR:openmw-server>/Build-OpenMWServerAuthority.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/files/server/server-setup-guide.md"
        "$<TARGET_FILE_DIR:openmw-server>/server-setup-guide.md"
    COMMAND_EXPAND_LISTS
    VERBATIM
)
