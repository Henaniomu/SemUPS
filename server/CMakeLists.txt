# Минимальная версия CMake
cmake_minimum_required(VERSION 3.10)

# Имя проекта
project(ServerProject)

# Устанавливаем стандарт C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Добавляем исполняемый файл и указываем файлы проекта
add_executable(server
    src/main.cpp
    src/server.cpp
)

# Установка путей для заголовочных файлов
target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
