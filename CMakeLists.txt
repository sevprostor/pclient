cmake_minimum_required(VERSION 3.5)

project(pclient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Поиск libwebsockets через PkgConfig (работает стабильно)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LWS REQUIRED libwebsockets)

# 2. НОВЫЙ СПОСОБ: Ищем MsgPack стандартным методом CMake для C++
find_package(msgpack REQUIRED)

add_executable(pclient main.cpp wsclient.h
    wsclient.cpp
    msgparser.h
    msgparser.cpp
    addressbook.h
    addressbook.cpp
    readme.md
    puheg.png
    log.h
    log.cpp
    console.h
    console.cpp)

# Указываем папки с заголовочными файлами (.h)
# Для msgpack теперь используем современную переменную msgpack_INCLUDE_DIRS или её Си-аналог
target_include_directories(pclient PRIVATE
    ${LWS_INCLUDE_DIRS}
    ${msgpack_INCLUDE_DIRS}
)

# Линкуем библиотеки
# msgpack автоматически предоставляет готовый таргет (цель) msgpackc или msgpackc-cxx
target_link_libraries(pclient PRIVATE
    ${LWS_LIBRARIES}
    msgpackc
)

include(GNUInstallDirs)
install(TARGETS pclient
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
