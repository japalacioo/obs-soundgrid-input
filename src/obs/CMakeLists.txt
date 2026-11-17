# Plugin OBS. Dos formas de encontrar libobs:
#  1) SG_OBS_SDK_DIR: SDK local generado por tools/make-obs-sdk.ps1 (cabeceras oficiales +
#     bibliotecas de importación de la instalación de OBS). Usado en este equipo.
#  2) find_package(libobs) / find_package(obs-frontend-api): árbol de compilación de OBS o
#     dependencias del obs-plugintemplate (CI).
set(_sg_have_obs FALSE)
if(SG_OBS_SDK_DIR AND EXISTS "${SG_OBS_SDK_DIR}/include/libobs/obs-module.h")
  add_library(sg_libobs INTERFACE)
  target_include_directories(sg_libobs INTERFACE "${SG_OBS_SDK_DIR}/include/libobs")
  if(WIN32)
    target_link_libraries(sg_libobs INTERFACE "${SG_OBS_SDK_DIR}/lib/obs.lib")
  endif()
  add_library(sg_frontend INTERFACE)
  target_include_directories(sg_frontend INTERFACE "${SG_OBS_SDK_DIR}/include/frontend-api")
  if(WIN32 AND EXISTS "${SG_OBS_SDK_DIR}/lib/obs-frontend-api.lib")
    target_link_libraries(sg_frontend INTERFACE "${SG_OBS_SDK_DIR}/lib/obs-frontend-api.lib")
  endif()
  set(_sg_obs_target sg_libobs)
  set(_sg_fe_target sg_frontend)
  set(_sg_have_obs TRUE)
else()
  find_package(libobs QUIET)
  find_package(obs-frontend-api QUIET)
  if(TARGET OBS::libobs)
    set(_sg_obs_target OBS::libobs)
    set(_sg_fe_target OBS::obs-frontend-api)
    set(_sg_have_obs TRUE)
  endif()
endif()

if(NOT _sg_have_obs)
  message(WARNING "libobs no encontrado: se omite el plugin (el núcleo y las pruebas sí se compilan). "
                  "Ejecuta tools/make-obs-sdk.ps1 y configura -DSG_OBS_SDK_DIR=.deps/obs-sdk")
  return()
endif()

add_library(obs-soundgrid-input MODULE
  plugin-main.cpp
  ObsSettingsStore.h
  ObsSoundGridSource.h ObsSoundGridSource.cpp
  Diagnostics.h Diagnostics.cpp
)
target_link_libraries(obs-soundgrid-input PRIVATE sg_core ${_sg_obs_target} ${_sg_fe_target})
set(SG_PLUGIN_AUTHOR "Joan Palacio")
target_compile_definitions(obs-soundgrid-input PRIVATE SG_PLUGIN_VERSION="${PROJECT_VERSION}"
                                                       SG_PLUGIN_AUTHOR="${SG_PLUGIN_AUTHOR}")
set_target_properties(obs-soundgrid-input PROPERTIES PREFIX "")
if(MSVC)
  # Cabeceras de libobs: avisos que no son nuestros.
  target_compile_options(obs-soundgrid-input PRIVATE /wd4244 /wd4267 /wd4100 /wd4201)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)
  target_sources(obs-soundgrid-input PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()
if(APPLE)
  set_target_properties(obs-soundgrid-input PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION plugin)
endif()

# Copia de datos (locales) junto al binario para pruebas y empaquetado.
add_custom_command(TARGET obs-soundgrid-input POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:obs-soundgrid-input>/data)

# Host de integración: carga libobs real (obs.dll) sin interfaz y verifica la fuente.
if(SG_BUILD_TESTS AND WIN32)
  add_executable(sg_obs_host_test ${CMAKE_SOURCE_DIR}/tests/integration/obs_host_test.cpp)
  target_link_libraries(sg_obs_host_test PRIVATE sg_core ${_sg_obs_target})
  target_compile_definitions(sg_obs_host_test PRIVATE
    SG_PLUGIN_PATH="$<TARGET_FILE:obs-soundgrid-input>"
    SG_PLUGIN_DATA="$<TARGET_FILE_DIR:obs-soundgrid-input>/data")
  if(MSVC)
    target_compile_options(sg_obs_host_test PRIVATE /wd4244 /wd4267 /wd4100 /wd4201)
  endif()
  add_dependencies(sg_obs_host_test obs-soundgrid-input)
  # Requiere OBS instalado (obs.dll y dependencias). Etiqueta "obs".
  set(SG_OBS_BIN_DIR "$ENV{ProgramFiles}/obs-studio/bin/64bit" CACHE PATH "Carpeta bin de una instalación de OBS")
  if(EXISTS "${SG_OBS_BIN_DIR}/obs.dll")
    add_test(NAME OBS_Host_Integration COMMAND sg_obs_host_test WORKING_DIRECTORY "${SG_OBS_BIN_DIR}")
    set_tests_properties(OBS_Host_Integration PROPERTIES LABELS "obs" TIMEOUT 120
      ENVIRONMENT "PATH=${SG_OBS_BIN_DIR}" PASS_REGULAR_EXPRESSION "RESULTADO: PASS")
  endif()
endif()
