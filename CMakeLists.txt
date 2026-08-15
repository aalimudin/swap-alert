cmake_minimum_required(VERSION 3.24)

project(SwapAlert
    VERSION 0.1.0
    DESCRIPTION "A lightweight tray utility for monitoring system swap usage"
    HOMEPAGE_URL "https://github.com/aalimudin/swap-alert"
    LANGUAGES CXX
)

if(APPLE)
    enable_language(OBJCXX)
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Swap Alert currently supports macOS and Linux")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
set(CMAKE_AUTOMOC ON)
include(CTest)

set(SWAP_ALERT_QT_COMPONENTS Core Widgets)
if(BUILD_TESTING)
    list(APPEND SWAP_ALERT_QT_COMPONENTS Test)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS ${SWAP_ALERT_QT_COMPONENTS})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
endif()

qt_standard_project_setup()

set(SWAP_ALERT_CORE_SOURCES
    src/core/AlertEngine.cpp
    src/core/DiagnosticLogger.cpp
    src/core/Logging.cpp
    src/core/ProcessGrouping.cpp
    src/core/SettingsStore.cpp
    src/core/SwapMonitor.cpp
)

if(APPLE)
    set(SWAP_ALERT_PLATFORM_SOURCES
        src/platform/macos/MacAutostartService.mm
        src/platform/macos/MacContinuousClock.mm
        src/platform/macos/MacNotificationService.mm
        src/platform/macos/MacProcessService.mm
        src/platform/macos/MacSwapReader.mm
        src/platform/macos/MacSystemEventMonitor.mm
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SWAP_ALERT_PLATFORM_SOURCES
        src/platform/linux/LinuxAutostartService.cpp
        src/platform/linux/LinuxBootClock.cpp
        src/platform/linux/LinuxNotificationService.cpp
        src/platform/linux/LinuxProcessService.cpp
        src/platform/linux/LinuxSwapReader.cpp
        src/platform/linux/LinuxSystemEventMonitor.cpp
    )
endif()

set(SWAP_ALERT_UI_SOURCES
    src/ui/CleanupDialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/StatusDialog.cpp
    src/ui/TrayController.cpp
    src/ui/WarningDialog.cpp
)

qt_add_executable(SwapAlert
    src/app/main.cpp
    ${SWAP_ALERT_CORE_SOURCES}
    ${SWAP_ALERT_PLATFORM_SOURCES}
    ${SWAP_ALERT_UI_SOURCES}
)

target_include_directories(SwapAlert PRIVATE src)
target_compile_definitions(SwapAlert PRIVATE SWAP_ALERT_VERSION="${PROJECT_VERSION}")
target_link_libraries(SwapAlert PRIVATE
    Qt6::Core
    Qt6::Widgets
)

if(APPLE)
    target_link_libraries(SwapAlert PRIVATE
        "-framework AppKit"
        "-framework ServiceManagement"
        "-framework UserNotifications"
    )
    set_source_files_properties(${SWAP_ALERT_PLATFORM_SOURCES}
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(SwapAlert PRIVATE Qt6::DBus)
endif()

set_target_properties(SwapAlert PROPERTIES
    OUTPUT_NAME "Swap Alert"
)
if(APPLE)
    set_target_properties(SwapAlert PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Swap Alert"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.swapalert.app"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    )
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set_target_properties(SwapAlert PROPERTIES OUTPUT_NAME "swap-alert")
endif()

install(TARGETS SwapAlert
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
    COMPONENT Runtime
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES packaging/linux/com.swapalert.app.desktop
        DESTINATION share/applications)
    install(FILES packaging/linux/com.swapalert.app.metainfo.xml
        DESTINATION share/metainfo)
    install(FILES resources/com.swapalert.app.svg
        DESTINATION share/icons/hicolor/scalable/apps)
endif()

if(BUILD_TESTING)
    if(APPLE)
        set(SWAP_READER_TEST_SOURCE src/platform/macos/MacSwapReader.mm)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(SWAP_READER_TEST_SOURCE src/platform/linux/LinuxSwapReader.cpp)
    endif()
qt_add_executable(SwapAlertTests
    tests/AlertEngineTests.cpp
    src/core/AlertEngine.cpp
    ${SWAP_READER_TEST_SOURCE}
)
target_include_directories(SwapAlertTests PRIVATE src)
target_link_libraries(SwapAlertTests PRIVATE Qt6::Core Qt6::Test)
if(APPLE)
    set_source_files_properties(src/platform/macos/MacSwapReader.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
endif()

add_test(NAME SwapAlertTests COMMAND SwapAlertTests)

qt_add_executable(SwapMonitorTests
    tests/SwapMonitorTests.cpp
    src/core/AlertEngine.cpp
    src/core/Logging.cpp
    src/core/SettingsStore.cpp
    src/core/SwapMonitor.cpp
)
target_include_directories(SwapMonitorTests PRIVATE src)
target_link_libraries(SwapMonitorTests PRIVATE Qt6::Core Qt6::Test)

add_test(NAME SwapMonitorTests COMMAND SwapMonitorTests)

qt_add_executable(ProcessGroupingTests
    tests/ProcessGroupingTests.cpp
    src/core/ProcessGrouping.cpp
)
target_include_directories(ProcessGroupingTests PRIVATE src)
target_link_libraries(ProcessGroupingTests PRIVATE Qt6::Core Qt6::Test)

add_test(NAME ProcessGroupingTests COMMAND ProcessGroupingTests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    qt_add_executable(LinuxProcessServiceTests
        tests/LinuxProcessServiceTests.cpp
        src/core/Logging.cpp
        src/core/ProcessGrouping.cpp
        src/platform/linux/LinuxProcessService.cpp
    )
    target_include_directories(LinuxProcessServiceTests PRIVATE src)
    target_link_libraries(LinuxProcessServiceTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME LinuxProcessServiceTests COMMAND LinuxProcessServiceTests)
endif()

qt_add_executable(UiSafetyTests
    tests/UiSafetyTests.cpp
    src/core/AlertEngine.cpp
    src/core/SettingsStore.cpp
    src/ui/CleanupDialog.cpp
    src/ui/SettingsDialog.cpp
)
target_include_directories(UiSafetyTests PRIVATE src)
target_link_libraries(UiSafetyTests PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)

add_test(NAME UiSafetyTests COMMAND UiSafetyTests)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SWAP_ALERT_PACKAGE_LICENSE "LicenseRef-Proprietary" CACHE STRING
        "SPDX license identifier used by native Linux packages")

    set(CPACK_PACKAGE_NAME "swap-alert")
    set(CPACK_PACKAGE_VENDOR "Swap Alert")
    set(CPACK_PACKAGE_CONTACT "Ahmad Alimuddin <achmad_aal@hotmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_STRIP_FILES TRUE)

    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_RELEASE 1)
    set(CPACK_RPM_PACKAGE_LICENSE "${SWAP_ALERT_PACKAGE_LICENSE}")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications
        /usr/share/icons
        /usr/share/icons/hicolor
        /usr/share/icons/hicolor/scalable
        /usr/share/icons/hicolor/scalable/apps
        /usr/share/metainfo
    )

    include(CPack)
endif()
