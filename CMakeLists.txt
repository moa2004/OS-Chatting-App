cmake_minimum_required(VERSION 3.16)
project(OSChatSystems LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN)

include_directories(${CMAKE_SOURCE_DIR}/src)

# Unified launcher with runtime selection between socket and shared-memory chats.
add_executable(chat_app
    src/app_main.cpp
    src/socket_chat.cpp
    src/shm_chat.cpp
    src/ui_helpers.h
)
set_target_properties(chat_app PROPERTIES WIN32_EXECUTABLE ON)

target_link_libraries(chat_app
    ws2_32
    msimg32
    comctl32
    user32
    gdi32
)

# Link statically to libstdc++/libgcc when using MinGW so the exe runs on machines
# without those DLLs installed. MSVC builds ignore these flags.
if (MINGW)
    target_link_options(chat_app PRIVATE
        -mwindows
        -municode
        -static
        -static-libgcc
        -static-libstdc++
        -pthread
    )
else()
    target_link_options(chat_app PRIVATE -mwindows -municode)
endif()
