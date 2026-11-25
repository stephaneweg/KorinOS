# Le style korin de QWidgets (Korin OS) : les contrôles dessinés comme ceux d'UIKit (tools/qt/style,
# docs/etude/QT.md § style). Recopié dans qtbase/src/plugins/styles/korin par tools/qt/build.sh.
# Il ne dépend pas d'UIKit : les jetons du thème et les métriques viennent du thème de la plateforme korin
# (QPlatformTheme::themeHint, indications propres à Korin), sinon de la palette de l'application.

qt_internal_add_plugin(QKorinStylePlugin
    OUTPUT_NAME qkorinstyle
    PLUGIN_TYPE styles
    SOURCES
        main.cpp
        qkorinstyle.cpp qkorinstyle.h
    DEFINES
        QT_NO_FOREACH
    LIBRARIES
        Qt::Core
        Qt::Gui
        Qt::GuiPrivate
        Qt::Widgets
)
