qt_add_executable(qtcraft-headless
    main.cpp
    QcHeadlessGame.h QcHeadlessGame.cpp
    QcMapRenderer.h QcMapRenderer.cpp
    QcShowcaseTour.h QcShowcaseTour.cpp
    QcSoundExporter.h QcSoundExporter.cpp
    QcServerConsole.h QcServerConsole.cpp
    QcStdinReader.h QcStdinReader.cpp
    QcWorldStats.h QcWorldStats.cpp
)
qc_configure_target(qtcraft-headless)
set_target_properties(qtcraft-headless PROPERTIES MACOSX_BUNDLE OFF)
target_link_libraries(qtcraft-headless PRIVATE qc_server qc_clientcore Qt::Core Qt::Gui)
