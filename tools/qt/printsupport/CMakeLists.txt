# L'impression de Qt sur PrinterKit (Korin OS ; tools/qt/printsupport, docs/etude/QT.md § impression) : les
# imprimantes de Korin pour QPrinter, un moteur de dessin vers print_* (tracés, images), le dialogue de PrinterKit pour
# QPrintDialog. PrinterKit est ouvert à l'exécution (armpi_kit_open) : pas de -lprinterkit.
qt_internal_add_plugin(QKorinPrinterSupportPlugin
    OUTPUT_NAME korinprintersupport
    PLUGIN_TYPE printsupport
    DEFAULT_IF TRUE
    SOURCES
        main.cpp
        qkorinprintersupport.cpp qkorinprintersupport.h
    INCLUDE_DIRECTORIES
        ../../../printsupport/kernel
        ${KORIN_UIKIT_INCLUDE_DIRS}
        ${KORIN_PRINTERKIT_INCLUDE_DIR}
    LIBRARIES
        Qt::Core
        Qt::CorePrivate
        Qt::Gui
        Qt::GuiPrivate
        Qt::Widgets
        Qt::PrintSupport
        Qt::PrintSupportPrivate
)
