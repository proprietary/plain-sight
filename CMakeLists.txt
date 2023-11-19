cmake_minimum_required(VERSION 3.20)
project(plain_sight)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(AVFILTER REQUIRED IMPORTED_TARGET libavfilter)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)

add_subdirectory(third_party)

add_library(plain_sight)
target_sources(
    plain_sight
    PUBLIC
    plain_sight/video_generator.h
    plain_sight/video_generator.cc
    plain_sight/decoder.h plain_sight/decoder.cc
    plain_sight/encoder.h plain_sight/encoder.cc
    plain_sight/qr_codes.h plain_sight/qr_codes.cc
    plain_sight/util.h plain_sight/util.cc
)
target_include_directories(
    plain_sight
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(
    plain_sight
    PUBLIC
    com_github_nayuki_QRCodeGenerator
    PkgConfig::AVCODEC PkgConfig::AVFORMAT PkgConfig::AVUTIL PkgConfig::SWSCALE
)

enable_testing()
add_executable(
    video_generator_test
    plain_sight/video_generator_test.cc
)
target_link_libraries(
    video_generator_test
    plain_sight
    GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(video_generator_test)