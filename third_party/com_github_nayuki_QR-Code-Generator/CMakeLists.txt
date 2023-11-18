cmake_minimum_required(VERSION 3.20)

project(com_github_nayuki_QRCodeGenerator)

set(CMAKE_CXX_STANDARD 11)

add_library(com_github_nayuki_QRCodeGenerator)
target_sources(com_github_nayuki_QRCodeGenerator
    PUBLIC QR-Code-generator/cpp/qrcodegen.hpp
    PRIVATE QR-Code-generator/cpp/qrcodegen.cpp)
target_include_directories(com_github_nayuki_QRCodeGenerator
    PUBLIC QR-Code-generator/cpp)