cmake_minimum_required(VERSION 3.28)

project(obs-soundgrid-input VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SG_BUILD_TESTS "Compilar pruebas unitarias e de integración (GoogleTest)" ON)
option(SG_BUILD_OBS_PLUGIN "Compilar el plugin de OBS (requiere libobs)" ON)
option(SG_ENABLE_ASIO "Adaptador ASIO (solo Windows)" ON)
option(SG_ENABLE_COREAUDIO "Adaptador Core Audio (solo macOS)" ON)
option(SG_SANITIZERS "ASAN/UBSAN en compiladores que lo soporten" OFF)
set(SG_OBS_SDK_DIR "" CACHE PATH "SDK local de libobs (include/ + lib/), ver tools/make-obs-sdk.ps1")

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  # Binario universal por defecto; la compatibilidad del driver Waves se certifica por CPU (ADR-007).
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
  if(SG_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

if(SG_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(src)

if(SG_BUILD_TESTS)
  add_subdirectory(tests)
endif()
