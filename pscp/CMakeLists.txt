cmake_minimum_required(VERSION 3.5)

project(pscp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pscp main.cpp
    events.h
    events.cpp)

include(GNUInstallDirs)
install(TARGETS pscp
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
