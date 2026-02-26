cmake_minimum_required(VERSION 3.20)

project(ImWebBrowser LANGUAGES CXX)

add_executable(ImWebBrowser main.cpp)

set_target_properties(ImWebBrowser PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
)