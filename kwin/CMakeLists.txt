cmake_minimum_required(VERSION 3.16)

project(EdgeVolumeKWin VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(ECM 6.10 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

set(KDE_INSTALL_USE_QT_SYS_PATHS ON)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core DBus Widgets)
find_package(KF6CoreAddons 6.10 REQUIRED)
find_package(KWin 6.3.6 CONFIG REQUIRED)

kcoreaddons_add_plugin(kdevolume
    INSTALL_NAMESPACE "kwin/plugins"
)

target_sources(kdevolume PRIVATE
    src/main.cpp
)

target_link_libraries(kdevolume PRIVATE
    KWin::kwin
    Qt6::Core
    Qt6::DBus
    Qt6::Widgets
)
