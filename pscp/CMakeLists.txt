cmake_minimum_required(VERSION 3.5)

project(phauler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(phauler main.cpp
    events.h
    events.cpp
    transport.h
    transport.cpp
    config.h
    config.cpp
    hauler.h
    hauler.cpp)

include(GNUInstallDirs)
install(TARGETS phauler
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
