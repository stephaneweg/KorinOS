# Les programmes d'essai du port de Qt (docs/etude/QT.md) : construits par tools/qt/test.sh avec le Qt de Korin.
cmake_minimum_required(VERSION 3.21)
project(korin_qt_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui GuiPrivate Widgets)

qt_add_executable(qtcore_test qtcore_test.cpp)
target_link_libraries(qtcore_test PRIVATE Qt6::Core)

qt_add_executable(qtwidgets_test qtwidgets_test.cpp)
target_link_libraries(qtwidgets_test PRIVATE Qt6::Widgets Qt6::GuiPrivate)

qt_add_executable(qtfont_test qtfont_test.cpp)
target_link_libraries(qtfont_test PRIVATE Qt6::Widgets)
# (la police Edwin de MuseScore, si sa source est là : tools/musescore)
set(EDWIN "$ENV{HOME}/korin-mscore/src/musescore/fonts/edwin/Edwin-Roman.otf")
if(EXISTS "${EDWIN}")
    qt_add_resources(qtfont_test "fonts" PREFIX "/" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES "${CMAKE_CURRENT_BINARY_DIR}/Edwin-Roman.otf")
    configure_file("${EDWIN}" "${CMAKE_CURRENT_BINARY_DIR}/Edwin-Roman.otf" COPYONLY)
endif()

find_package(Qt6 COMPONENTS Quick Qml)
if(Qt6Quick_FOUND)
    qt_policy(SET QTP0001 NEW)	# (le module sous qrc:/qt/qml, où loadFromModule le cherche)
    qt_add_executable(qtquick_test qtquick_test.cpp)
    qt_add_qml_module(qtquick_test URI KorinTest VERSION 1.0 QML_FILES Main.qml NO_PLUGIN)
    target_link_libraries(qtquick_test PRIVATE Qt6::Quick Qt6::Qml)
endif()
