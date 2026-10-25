# Public client setup must include every advertised wizard entry point and plugin.
if(NOT DEFINED CLIENT_DIR)
    message(FATAL_ERROR "CLIENT_DIR is required")
endif()
foreach(name IN ITEMS openmw.exe openmw-wizard.exe openmw-iniimporter.exe
        Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Svg.dll
        platforms/qwindows.dll imageformats/qsvg.dll iconengines/qsvgicon.dll qt.conf)
    if(NOT EXISTS "${CLIENT_DIR}/${name}" OR IS_DIRECTORY "${CLIENT_DIR}/${name}")
        message(FATAL_ERROR "Client setup runtime is incomplete: ${name}")
    endif()
endforeach()
message(STATUS "Windows setup wizard, importer and Qt runtime are present")
