cmake_minimum_required(VERSION 3.20)

project(com_github_nayuki_QRCodeGenerator)

add_library(com_github_nayuki_QRCodeGenerator)
target_sources(com_github_nayuki_QRCodeGenerator
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/QR-Code-generator/cpp/qrcodegen.hpp
    PRIVATE QR-Code-generator/cpp/qrcodegen.hpp QR-Code-generator/cpp/qrcodegen.cpp)
target_include_directories(com_github_nayuki_QRCodeGenerator
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/QR-Code-generator/cpp)
set_target_properties(com_github_nayuki_QRCodeGenerator PROPERTIES
    CXX_STANDARD 11)
