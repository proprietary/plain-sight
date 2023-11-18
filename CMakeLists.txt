cmake_minimum_required(VERSION 3.20)
project(plain_sight)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)

include_directories(${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS})

add_subdirectory(third_party)

add_library(plain_sight)
target_sources(
    plain_sight PUBLIC
    plain_sight/video_generator.h
    plain_sight/video_generator.cc
    plain_sight/decoder.h plain_sight/decoder.cc
    plain_sight/encoder.h plain_sight/encoder.cc
    plain_sight/qr_codes.h plain_sight/qr_codes.cc
)
target_include_directories(plain_sight PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(
    plain_sight
    PRIVATE ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES}
    com_github_nayuki_QRCodeGenerator
)