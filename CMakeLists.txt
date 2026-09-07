cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBISAC_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_isac.c
)

SET(LIBISAC_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libisac.a
)

SET(LIBISAC_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libisac
        "${LIBISAC_SRC}"
        "${LIBISAC_LIB}"
        []
        ""
        "${LIBISAC_INC}"
        ""
        "1")
