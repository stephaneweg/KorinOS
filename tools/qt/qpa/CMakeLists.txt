# La QPA korin (Korin OS) : fenêtres, événements, thème et menus d'UIKit (tools/qt, docs/etude/QT.md).
# KORIN_UIKIT_INCLUDE_DIRS, KORIN_UIKIT_LIBRARY : uikit.h et libuikit.a (tools/pc/apps.sh), donnés par build.sh.

qt_find_package(WrapFreetype MODULE PROVIDED_TARGETS WrapFreetype::WrapFreetype)

qt_internal_add_plugin(QKorinIntegrationPlugin
    OUTPUT_NAME qkorin
    PLUGIN_TYPE platforms
    DEFAULT_IF "korin" IN_LIST QT_QPA_PLATFORMS
    SOURCES
        main.cpp
        korin_ui.c korin_ui.h
        qkorinintegration.cpp qkorinintegration.h
        qkorinwindow.cpp qkorinwindow.h
        qkorintheme.cpp qkorintheme.h
    INCLUDE_DIRECTORIES
        ${KORIN_UIKIT_INCLUDE_DIRS}
    DEFINES
        QT_NO_FOREACH
    LIBRARIES
        Qt::Core
        Qt::CorePrivate
        Qt::Gui
        Qt::GuiPrivate
        ${KORIN_UIKIT_LIBRARY}
)

qt_internal_extend_target(QKorinIntegrationPlugin CONDITION QT_FEATURE_freetype
    LIBRARIES
        WrapFreetype::WrapFreetype
)
