cmake_minimum_required(VERSION 3.24)

project(SwapAlert VERSION 0.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)

qt_standard_project_setup()

set(SWAP_ALERT_CORE_SOURCES
    src/core/AlertEngine.cpp
    src/core/SettingsStore.cpp
    src/core/SwapMonitor.cpp
)

set(SWAP_ALERT_PLATFORM_SOURCES
    src/platform/macos/MacAutostartService.mm
    src/platform/macos/MacContinuousClock.mm
    src/platform/macos/MacNotificationService.mm
    src/platform/macos/MacProcessService.mm
    src/platform/macos/MacSwapReader.mm
)

set(SWAP_ALERT_UI_SOURCES
    src/ui/CleanupDialog.cpp
    src/ui/SettingsDialog.cpp
    src/ui/StatusDialog.cpp
    src/ui/TrayController.cpp
    src/ui/WarningDialog.cpp
)

qt_add_executable(SwapAlert MACOSX_BUNDLE
    src/app/main.cpp
    ${SWAP_ALERT_CORE_SOURCES}
    ${SWAP_ALERT_PLATFORM_SOURCES}
    ${SWAP_ALERT_UI_SOURCES}
)

target_include_directories(SwapAlert PRIVATE src)
target_link_libraries(SwapAlert PRIVATE
    Qt6::Core
    Qt6::Widgets
    "-framework AppKit"
    "-framework ServiceManagement"
    "-framework UserNotifications"
)

set_source_files_properties(${SWAP_ALERT_PLATFORM_SOURCES}
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

set_target_properties(SwapAlert PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Swap Alert"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.swapalert.app"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    OUTPUT_NAME "Swap Alert"
)

enable_testing()

qt_add_executable(SwapAlertTests
    tests/AlertEngineTests.cpp
    src/core/AlertEngine.cpp
    src/platform/macos/MacSwapReader.mm
)
target_include_directories(SwapAlertTests PRIVATE src)
target_link_libraries(SwapAlertTests PRIVATE Qt6::Core Qt6::Test)
set_source_files_properties(src/platform/macos/MacSwapReader.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

add_test(NAME SwapAlertTests COMMAND SwapAlertTests)

qt_add_executable(SwapMonitorTests
    tests/SwapMonitorTests.cpp
    src/core/AlertEngine.cpp
    src/core/SettingsStore.cpp
    src/core/SwapMonitor.cpp
)
target_include_directories(SwapMonitorTests PRIVATE src)
target_link_libraries(SwapMonitorTests PRIVATE Qt6::Core Qt6::Test)

add_test(NAME SwapMonitorTests COMMAND SwapMonitorTests)
