cmake_minimum_required(VERSION 3.21)

project(afmu VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)

# 二维码编码交给 libqrencode（Debian/Ubuntu: libqrencode-dev，Fedora: qrencode-devel，
# Arch: qrencode，openSUSE: qrencode-devel）
find_package(PkgConfig REQUIRED)
pkg_check_modules(QRENCODE REQUIRED IMPORTED_TARGET libqrencode)

qt_standard_project_setup(REQUIRES 6.5)

set(AFMU_CPP
    src/main.cpp
    src/Protocol.h            src/Protocol.cpp
    src/QrCode.h              src/QrCode.cpp
    src/PathSafety.h          src/PathSafety.cpp
    src/Config.h              src/Config.cpp
    src/I18n.h                src/I18n.cpp
    src/Discovery.h           src/Discovery.cpp
    src/Models.h              src/Models.cpp
    src/PeerClient.h          src/PeerClient.cpp
    src/TransferModel.h       src/TransferModel.cpp
    src/AuthRequests.h        src/AuthRequests.cpp
    src/HttpServer.h          src/HttpServer.cpp
    src/AppController.h       src/AppController.cpp
)

qt_add_executable(afmu ${AFMU_CPP})

set(AFMU_QML
    qml/Main.qml
    qml/Theme.qml
    qml/Tr.qml
    qml/AppIcon.qml
    qml/TitleBar.qml
    qml/WindowButton.qml
    qml/NavRail.qml
    qml/NavItem.qml
    qml/FlatButton.qml
    qml/IconButton.qml
    qml/AppTextField.qml
    qml/AppSwitch.qml
    qml/AppCheckBox.qml
    qml/AppScrollBar.qml
    qml/Card.qml
    qml/SectionLabel.qml
    qml/StatBadge.qml
    qml/StatusDot.qml
    qml/ProgressLine.qml
    qml/EmptyState.qml
    qml/Toast.qml
    qml/ConfirmDialog.qml
    qml/PromptDialog.qml
    qml/DevicesPage.qml
    qml/BrowsePage.qml
    qml/TransfersPage.qml
    qml/ServerPage.qml
    qml/SettingsPage.qml
    qml/PairDialog.qml
    qml/AuthWaitDialog.qml
    qml/IncomingAuthDialog.qml
)

# 全部平铺到模块根目录，保证每个文件都能作为 Afmu 的类型使用
foreach(f IN LISTS AFMU_QML)
    get_filename_component(_fn "${f}" NAME)
    set_source_files_properties("${f}" PROPERTIES QT_RESOURCE_ALIAS "${_fn}")
endforeach()

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Tr.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(afmu
    URI Afmu
    VERSION 1.0
    QML_FILES ${AFMU_QML}
    # QML_ELEMENT 的类型必须走模块的 SOURCES，才会生成注册代码
    SOURCES
        src/QrImage.h src/QrImage.cpp
)

# 生成的 QML 类型注册代码是 #include <QrImage.h>，得让 src/ 在搜索路径里
target_include_directories(afmu PRIVATE src)

target_link_libraries(afmu PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    PkgConfig::QRENCODE
)

install(TARGETS afmu RUNTIME DESTINATION bin)
