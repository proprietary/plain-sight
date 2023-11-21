cmake_minimum_required(VERSION 3.20)
project(quirc)

set(LIB_OBJ
    quirc/lib/quirc.h
    quirc/lib/quirc_internal.h
    quirc/lib/decode.c
    quirc/lib/identify.c
    quirc/lib/quirc.c
    quirc/lib/version_db.c
)

add_library(quirc)
target_sources(quirc PRIVATE ${LIB_OBJ} PUBLIC quirc/lib/quirc.h)
target_include_directories(quirc PUBLIC quirc/lib)