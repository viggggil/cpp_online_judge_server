find_package(PkgConfig QUIET)

if (PkgConfig_FOUND)
    pkg_check_modules(PC_HIREDIS QUIET hiredis)
endif()

find_path(HIREDIS_INCLUDE_DIR
    NAMES hiredis/hiredis.h
    HINTS ${PC_HIREDIS_INCLUDE_DIRS}
)

find_library(HIREDIS_LIBRARY
    NAMES hiredis
    HINTS ${PC_HIREDIS_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(hiredis
    REQUIRED_VARS HIREDIS_LIBRARY HIREDIS_INCLUDE_DIR
)

if (hiredis_FOUND)
    set(hiredis_INCLUDE_DIRS ${HIREDIS_INCLUDE_DIR})
    set(hiredis_LIBRARIES ${HIREDIS_LIBRARY})

    if (NOT TARGET hiredis::hiredis)
        add_library(hiredis::hiredis UNKNOWN IMPORTED)
        set_target_properties(hiredis::hiredis PROPERTIES
            IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}"
        )
    endif()
endif()

mark_as_advanced(HIREDIS_INCLUDE_DIR HIREDIS_LIBRARY)