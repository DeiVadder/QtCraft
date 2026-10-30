# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# The API reference with QDoc (decision E-939): target `docs`, not part of the default build
# (`cmake --build --preset debug --target docs`). scripts/api-docs.py stages the headers and QML
# files with their topic commands, runs qdoc with the include paths of compile_commands.json and
# writes HTML to <build>/docs/api, the qdoc warnings (places in src/) to <build>/docs/qdoc-warnings.txt.
# QC_QDOC is the qdoc program (default: bin/qdoc of the Qt found); QC_QT_DOCS the folder with Qt's
# documentation index files for links to Qt types (default: Docs/Qt-<version> next to the Qt
# installation, where the installer and aqt put it). Without qdoc there is no target.

find_program(QC_QDOC NAMES qdoc HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/bin"
    DOC "qdoc for the API reference (target docs)")
set(QC_QT_DOCS "" CACHE PATH "Folder with Qt's documentation index files (<module>/<module>.index); empty: next to Qt")
find_package(Python3 COMPONENTS Interpreter QUIET)

if(QC_QDOC AND Python3_Interpreter_FOUND)
    set(_qc_docs_arguments --qdoc "${QC_QDOC}" --compile-commands "${CMAKE_BINARY_DIR}/compile_commands.json"
        --out "${CMAKE_BINARY_DIR}/docs/api")
    if(QC_QT_DOCS)
        list(APPEND _qc_docs_arguments --indexes "${QC_QT_DOCS}")
    endif()
    add_custom_target(docs
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/api-docs.py" ${_qc_docs_arguments}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "API reference with QDoc into ${CMAKE_BINARY_DIR}/docs/api"
        USES_TERMINAL
        VERBATIM)
else()
    message(STATUS "QtCraft: no qdoc or no Python 3, no target docs (QC_QDOC)")
endif()
