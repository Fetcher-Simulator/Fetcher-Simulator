include_directories(SYSTEM ${GTEST_INCLUDE_DIRS})
include_directories(SYSTEM ${GMOCK_INCLUDE_DIRS})

file(GLOB UNITTEST_SRC_FILES
    main.cpp

    options.cpp

    mwworld/teststore.cpp
    mwworld/testduration.cpp
    mwworld/testtimestamp.cpp
    mwworld/testptr.cpp
    mwworld/testplayercrime.cpp
    mwworld/testplayerfaction.cpp
    mwworld/testweather.cpp

    mwdialogue/testkeywordsearch.cpp

    mwgui/tooltips.cpp
    mwgui/weightedsearch.cpp

    mwscript/testscripts.cpp

    mwmp/testservercontentregistry.cpp
    mwmp/testserverluarecordparser.cpp
    mwmp/testtypeddynamicrecordinstaller.cpp
    mwmp/testalchemyservice.cpp
    mwmp/testenchantingservice.cpp
    mwmp/testplayerspellbook.cpp
    mwmp/testcrimeservice.cpp
    mwmp/testobservationservice.cpp
    mwmp/testservercollisionlifecycle.cpp
    mwmp/testmechanicssnapshotregistry.cpp
    mwmp/testcollisioncellownership.cpp
    mwmp/testdoorstateauthority.cpp
    mwmp/testliveobservationruntime.cpp
    mwmp/testtopicpersistence.cpp
    mwmp/testfactionservice.cpp
)

if (MSVC)
    # these TUs take a lot of memory for codegen
    # in CI, they often go over the 8GB memory limit if another cl.exe is running at the same time
    # this option (make sure you're not looking at the docs for the identically-named linker option) stops four codegen threads being spun off
    # it hurts build time ~33% to enable this unnecessarily, though, so target it to the hungriest boys
    set_source_files_properties(
        mwworld/teststore.cpp
        PROPERTIES
        COMPILE_OPTIONS /cgthreads1
    )
endif()

source_group(apps\\openmw-tests FILES ${UNITTEST_SRC_FILES})

openmw_add_executable(openmw-tests ${UNITTEST_SRC_FILES})

if (BUILD_MULTIPLAYER OR BUILD_DEDICATED_SERVER)
    target_sources(openmw-tests PRIVATE
        ../openmw-server/ServerContentRegistry.cpp
        ../openmw-server/ServerLuaRecordParser.cpp
        ../openmw-server/PlayerDatabase.cpp
        ../openmw-server/DynamicRecordService.cpp
        ../openmw-server/AlchemyService.cpp
        ../openmw-server/EnchantingService.cpp
        ../openmw-server/CrimeService.cpp
        ../openmw-server/ObservationService.cpp
        ../openmw-server/MechanicsSnapshotRegistry.cpp
        ../openmw-server/CollisionCellOwnership.cpp
        ../openmw-server/DoorStateAuthority.cpp
        ../openmw-server/LiveObservationRuntime.cpp
        ../openmw-server/FactionService.cpp
    )
endif()

target_link_libraries(openmw-tests
    GTest::GTest
    GMock::GMock
    openmw-lib
)

# Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
    target_link_libraries(openmw-tests ${CMAKE_THREAD_LIBS_INIT})
endif()

if (WIN32)
    target_sources(openmw-tests PRIVATE ${CMAKE_SOURCE_DIR}/files/windows/other-apps.manifest)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(openmw-tests PRIVATE --coverage)
    target_link_libraries(openmw-tests gcov)
endif()

target_compile_definitions(openmw-tests
    PRIVATE OPENMW_DATA_DIR=u8"${CMAKE_CURRENT_BINARY_DIR}/data"
            OPENMW_PROJECT_SOURCE_DIR=u8"${PROJECT_SOURCE_DIR}")

if (MSVC AND PRECOMPILE_HEADERS_WITH_MSVC)
    target_precompile_headers(openmw-tests PRIVATE
        <boost/program_options/options_description.hpp>

        <gtest/gtest.h>

        <sol/sol.hpp>

        <algorithm>
        <filesystem>
        <fstream>
        <functional>
        <memory>
        <string>
        <vector>
    )
endif()
