cmake_minimum_required(VERSION 3.20)
project(plain_sight)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS "-fPIC ${CMAKE_CXX_FLAGS}")

# Dependencies

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(AVFILTER REQUIRED IMPORTED_TARGET libavfilter)
pkg_check_modules(SWSCALE REQUIRED IMPORTED_TARGET libswscale)

find_package(OpenCV REQUIRED)

add_subdirectory(third_party)


# Plain Sight core library:

add_library(plain_sight)
target_sources(
    plain_sight
    PUBLIC
    plain_sight/qr_codes.h plain_sight/qr_codes.cc
    plain_sight/decoder.h plain_sight/decoder.cc
    plain_sight/encoder.h plain_sight/encoder.cc
    plain_sight/util.h plain_sight/util.cc
    plain_sight/codec.h plain_sight/codec.cc
)
target_include_directories(
    plain_sight
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(
    plain_sight
    PUBLIC
    com_github_nayuki_QRCodeGenerator
    glog::glog
    PkgConfig::AVCODEC PkgConfig::AVFORMAT PkgConfig::AVUTIL PkgConfig::SWSCALE
    quirc
    fmt::fmt
    ${OpenCV_LIBS}
)
set_target_properties(
    plain_sight
    PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_compile_options(
    plain_sight
    PRIVATE
    -Wall -Wextra -Werror -Wpedantic
    $<$<CONFIG:Debug>:-fsanitize=address,undefined -fno-omit-frame-pointer -g3>
)
target_link_options(
    plain_sight
    PUBLIC
    $<$<CONFIG:DEBUG>:-fsanitize=address,undefined -fno-omit-frame-pointer>
)

#######################
#      Tests          #
#######################

enable_testing()
add_executable(
    codec_test
    plain_sight/codec_test.cc
)
target_link_libraries(
    codec_test
    plain_sight
    GTest::gtest_main
)
add_executable(
    qr_codes_test
    plain_sight/qr_codes_test.cc
)
target_link_libraries(
    qr_codes_test
    plain_sight
    GTest::gtest_main
    com_github_nayuki_QRCodeGenerator
)
include(GoogleTest)
gtest_discover_tests(codec_test)
gtest_discover_tests(qr_codes_test)