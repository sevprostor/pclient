cmake_minimum_required(VERSION 3.5)

project(PuhegWords-filesend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(filesend main.cpp


    transport.h
    transport.cpp
    config.h
    config.cpp
    log.h
    words.h
    words.cpp
)

include(GNUInstallDirs)
install(TARGETS filesend
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
