# FindHarfBuzz.cmake : HarfBuzz du paquet harfbuzz-dev de Korin (statique, /devtools/gcc ; tools/musescore,
# docs/etude/MUSESCORE.md). MuseScore (MUE_COMPILE_USE_SYSTEM_HARFBUZZ) attend la cible harfbuzz::harfbuzz de la
# configuration CMake de HarfBuzz, que le paquet n'a pas (seulement harfbuzz.pc). Le même HarfBuzz que Qt : un seul
# dans le binaire statique.
find_path(HARFBUZZ_INCLUDE_DIR NAMES hb.h PATH_SUFFIXES harfbuzz)
find_library(HARFBUZZ_LIBRARY NAMES harfbuzz)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HarfBuzz REQUIRED_VARS HARFBUZZ_LIBRARY HARFBUZZ_INCLUDE_DIR)
if(HarfBuzz_FOUND AND NOT TARGET harfbuzz::harfbuzz)
    find_package(Freetype REQUIRED)
    add_library(harfbuzz::harfbuzz STATIC IMPORTED)
    set_target_properties(harfbuzz::harfbuzz PROPERTIES
        IMPORTED_LOCATION "${HARFBUZZ_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${HARFBUZZ_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "Freetype::Freetype")
endif()
