cmake_minimum_required(VERSION 3.16)

project(StreamingGStreamer
        VERSION 2.0
        DESCRIPTION "CMP1103 - Streaming com GStreamer: audio RTP/UDP (C) e video H.264 MPEG-TS/UDP com GUI GTK4 (C++)"
        LANGUAGES C CXX)

# ==========================================
# Padrão da linguagem
# ==========================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Gera compile_commands.json (indexação/autocompletar no CLion e outros IDEs)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build padrão quando nenhum tipo é informado
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Tipo de build" FORCE)
endif()

option(BUILD_GUI "Compilar as interfaces GTK4 (servidor e cliente)" ON)

# ==========================================
# Dependências (via pkg-config)
# gstreamer-1.0 já traz glib-2.0/gobject-2.0 como dependências transitivas
# (inclui glib-unix.h, usado para tratar Ctrl+C).
# ==========================================
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.16)
if(BUILD_GUI)
    pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
endif()

message(STATUS "GStreamer encontrado: ${GST_VERSION}")

set(WARNINGS_C   -Wall -Wextra -Wno-unused-parameter)
set(WARNINGS_CXX -Wall -Wextra)

# ==========================================
# Atividades anteriores: áudio RTP/UDP em C
# ==========================================
function(add_streaming_executable target source)
    add_executable(${target} ${source} streaming_config.h)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${target} PRIVATE PkgConfig::GST)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE ${WARNINGS_C})
    endif()
endfunction()

add_streaming_executable(transmissor transmissor.c)
add_streaming_executable(receptor    receptor.c)

# ==========================================
# Parte 2: vídeo H.264 em MPEG-TS/UDP (C++17)
# ==========================================
add_library(streaming_core STATIC
    src/core/config.cpp
    src/core/stream_base.cpp
    src/core/transmitter.cpp
    src/core/receiver.cpp)
target_include_directories(streaming_core PUBLIC src/core)
target_link_libraries(streaming_core PUBLIC PkgConfig::GST)

# Versões de linha de comando (sem GTK)
add_executable(servidor_cli src/cli/servidor_cli.cpp)
target_link_libraries(servidor_cli PRIVATE streaming_core)
add_executable(cliente_cli src/cli/cliente_cli.cpp)
target_link_libraries(cliente_cli PRIVATE streaming_core)

set(CXX_TARGETS streaming_core servidor_cli cliente_cli)

if(BUILD_GUI)
    add_library(gui_common STATIC src/ui/gui_common.cpp)
    target_include_directories(gui_common PUBLIC src/ui)
    target_link_libraries(gui_common PUBLIC streaming_core PkgConfig::GTK4)

    add_executable(servidor src/ui/servidor_gui.cpp)
    target_link_libraries(servidor PRIVATE gui_common)

    add_executable(cliente src/ui/cliente_gui.cpp)
    target_link_libraries(cliente PRIVATE gui_common)

    list(APPEND CXX_TARGETS gui_common servidor cliente)
endif()

# Teste automático do núcleo: ctest --output-on-failure
enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE streaming_core)
list(APPEND CXX_TARGETS test_core)
add_test(NAME core_localhost COMMAND test_core 5600)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(t IN LISTS CXX_TARGETS)
        target_compile_options(${t} PRIVATE ${WARNINGS_CXX})
    endforeach()
endif()
