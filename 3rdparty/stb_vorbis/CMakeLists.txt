# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# stb_vorbis (Sean Barrett, MIT or public domain, THIRD_PARTY.md), unchanged: decodes the Ogg
# Vorbis music in the audio thread (decision E-1135). Built as C++ (the project enables no C
# compiler; stb_vorbis supports both), without the project's warnings, which are for its own code.
# Without stdio: the client hands it the file's bytes in memory (QcVorbisStream).
add_library(qc_stb_vorbis STATIC stb_vorbis.c)
set_source_files_properties(stb_vorbis.c PROPERTIES LANGUAGE CXX)
set_target_properties(qc_stb_vorbis PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(qc_stb_vorbis PUBLIC STB_VORBIS_NO_STDIO STB_VORBIS_NO_PUSHDATA_API)
if(MSVC)
    target_compile_options(qc_stb_vorbis PRIVATE /w)
else()
    target_compile_options(qc_stb_vorbis PRIVATE -w)
endif()
# The declarations come from the same file with STB_VORBIS_HEADER_ONLY; SYSTEM keeps the project's
# warnings away from them.
target_include_directories(qc_stb_vorbis SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
