cmake_minimum_required(VERSION 3.10)
project(hologram C)

set(CMAKE_C_STANDARD 11)

# Windows resource file (embeds the GIF)
enable_language(RC)

add_executable(hologram WIN32
    main.c
    resource.rc
)

target_link_libraries(hologram
    gdi32
    user32
    advapi32
)
