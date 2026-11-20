# Plugin OBS. Dos formas de encontrar libobs:
#  1) SG_OBS_SDK_DIR: SDK local generado por tools/make-obs-sdk.ps1 (cabeceras oficiales +
#     bibliotecas de importación de la instalación de OBS). Usado en este equipo.
#  2) find_package(libobs) / find_package(obs-frontend-api): árbol de compilación de OBS o
#     dependencias del obs-plugintemplate (CI).
set(_sg_have_obs FALSE)
if(SG_OBS_SDK_DIR AND EXISTS "${SG_OBS_SDK_DIR}/include/libobs/obs-module.h")
  add_library(sg_libobs INTERFACE)
  target_include_directories(sg_libobs INTERFACE "${SG_OBS_SDK_DIR}/include/libobs")
  if(EXISTS "${SG_OBS_SDK_DIR}/include/simde")
    target_include_directories(sg_libobs INTERFACE "${SG_OBS_SDK_DIR}/include")
  endif()
  if(WIN32)
    target_link_libraries(sg_libobs INTERFACE "${SG_OBS_SDK_DIR}/lib/obs.lib")
  elseif(APPLE)
    # Stubs .tbd generados por tools/make-obs-sdk.sh (universales aunque OBS.app no lo sea).
    target_link_libraries(sg_libobs INTERFACE "${SG_OBS_SDK_DIR}/lib/libobs.tbd")
  endif()
  add_library(sg_frontend INTERFACE)
  target_include_directories(sg_frontend INTERFACE "${SG_OBS_SDK_DIR}/include/frontend-api")
  if(WIN32 AND EXISTS "${SG_OBS_SDK_DIR}/lib/obs-frontend-api.lib")
    target_link_libraries(sg_frontend INTERFACE "${SG_OBS_SDK_DIR}/lib/obs-frontend-api.lib")
  elseif(APPLE AND EXISTS "${SG_OBS_SDK_DIR}/lib/obs-frontend-api.tbd")
    target_link_libraries(sg_frontend INTERFACE "${SG_OBS_SDK_DIR}/lib/obs-frontend-api.tbd")
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
  AlertBanner.h
  WavesWatchdog.h WavesWatchdog.cpp
  PluginSettings.h PluginSettings.cpp
  GlobalMonitor.h GlobalMonitor.cpp
  AudioMonitorSource.h AudioMonitorSource.cpp
)
if(WIN32)
  target_sources(obs-soundgrid-input PRIVATE AlertBannerWin.cpp)
  target_link_libraries(obs-soundgrid-input PRIVATE user32 gdi32)
else()
  target_sources(obs-soundgrid-input PRIVATE AlertBannerStub.cpp)
endif()
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
  set_target_properties(obs-soundgrid-input PROPERTIES
    BUNDLE TRUE BUNDLE_EXTENSION plugin
    MACOSX_BUNDLE_GUI_IDENTIFIER com.obs-soundgrid-input.plugin
    MACOSX_BUNDLE_BUNDLE_NAME obs-soundgrid-input
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 ${SG_PLUGIN_AUTHOR}. GPLv3"
    INSTALL_RPATH "@executable_path/../Frameworks")
  # En un bundle .plugin, OBS busca los datos (locale/) en Contents/Resources.
  add_custom_command(TARGET obs-soundgrid-input POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data
            $<TARGET_BUNDLE_CONTENT_DIR:obs-soundgrid-input>/Resources)
else()
  # Copia de datos (locales) junto al binario para pruebas y empaquetado.
  add_custom_command(TARGET obs-soundgrid-input POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:obs-soundgrid-input>/data)
endif()

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
  # Monitor de audio del Multiview con render real (Direct3D 11). Sin validar todavía en Windows.
  add_executable(sg_obs_monitor_test ${CMAKE_SOURCE_DIR}/tests/integration/obs_monitor_render_test.cpp)
  target_link_libraries(sg_obs_monitor_test PRIVATE sg_core ${_sg_obs_target})
  target_compile_definitions(sg_obs_monitor_test PRIVATE
    SG_PLUGIN_PATH="$<TARGET_FILE:obs-soundgrid-input>"
    SG_PLUGIN_DATA="$<TARGET_FILE_DIR:obs-soundgrid-input>/data"
    SG_GRAPHICS_MODULE="libobs-d3d11"
    SG_TEXT_MODULE_BIN="${SG_OBS_BIN_DIR}/../../obs-plugins/64bit/obs-text.dll"
    SG_TEXT_MODULE_DATA="${SG_OBS_BIN_DIR}/../../data/obs-plugins/obs-text"
    SG_SNAPSHOT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
  if(MSVC)
    target_compile_options(sg_obs_monitor_test PRIVATE /wd4244 /wd4267 /wd4100 /wd4201)
  endif()
  add_dependencies(sg_obs_monitor_test obs-soundgrid-input)
  if(EXISTS "${SG_OBS_BIN_DIR}/obs.dll")
    add_test(NAME OBS_Monitor_Render COMMAND sg_obs_monitor_test WORKING_DIRECTORY "${SG_OBS_BIN_DIR}")
    set_tests_properties(OBS_Monitor_Render PROPERTIES LABELS "obs" TIMEOUT 120
      ENVIRONMENT "PATH=${SG_OBS_BIN_DIR}" PASS_REGULAR_EXPRESSION "RESULTADO: PASS")
  endif()
endif()

# macOS: el mismo host contra libobs.framework de OBS.app. Solo la arquitectura de OBS.app
# (libobs no es universal); el plugin a probar puede cambiarse con SG_PLUGIN_BUNDLE=<.plugin>.
if(SG_BUILD_TESTS AND APPLE)
  set(SG_OBS_APP "/Applications/OBS.app" CACHE PATH "OBS.app para la prueba de integración")
  set(_sg_libobs_bin "${SG_OBS_APP}/Contents/Frameworks/libobs.framework/libobs")
  if(EXISTS "${_sg_libobs_bin}")
    execute_process(COMMAND lipo -archs "${_sg_libobs_bin}" OUTPUT_VARIABLE _sg_obs_archs OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE " " ";" _sg_obs_archs "${_sg_obs_archs}")
    add_executable(sg_obs_host_test ${CMAKE_SOURCE_DIR}/tests/integration/obs_host_test.cpp)
    target_link_libraries(sg_obs_host_test PRIVATE sg_core ${_sg_obs_target})
    set_target_properties(sg_obs_host_test PROPERTIES OSX_ARCHITECTURES "${_sg_obs_archs}"
      BUILD_RPATH "${SG_OBS_APP}/Contents/Frameworks")
    target_compile_definitions(sg_obs_host_test PRIVATE
      SG_PLUGIN_PATH="$<TARGET_FILE:obs-soundgrid-input>"
      SG_PLUGIN_DATA="$<TARGET_BUNDLE_CONTENT_DIR:obs-soundgrid-input>/Resources")
    add_dependencies(sg_obs_host_test obs-soundgrid-input)
    add_test(NAME OBS_Host_Integration COMMAND sg_obs_host_test)
    set_tests_properties(OBS_Host_Integration PROPERTIES LABELS "obs" TIMEOUT 120
      PASS_REGULAR_EXPRESSION "RESULTADO: PASS")
    # Monitor de audio del Multiview: render real por GPU (libobs-opengl) y comprobación por píxeles.
    add_executable(sg_obs_monitor_test ${CMAKE_SOURCE_DIR}/tests/integration/obs_monitor_render_test.cpp)
    target_link_libraries(sg_obs_monitor_test PRIVATE sg_core ${_sg_obs_target})
    set_target_properties(sg_obs_monitor_test PROPERTIES OSX_ARCHITECTURES "${_sg_obs_archs}"
      BUILD_RPATH "${SG_OBS_APP}/Contents/Frameworks")
    target_compile_definitions(sg_obs_monitor_test PRIVATE
      SG_PLUGIN_PATH="$<TARGET_FILE:obs-soundgrid-input>"
      SG_PLUGIN_DATA="$<TARGET_BUNDLE_CONTENT_DIR:obs-soundgrid-input>/Resources"
      SG_GRAPHICS_MODULE="${SG_OBS_APP}/Contents/Frameworks/libobs-opengl.dylib"
      SG_TEXT_MODULE_BIN="${SG_OBS_APP}/Contents/PlugIns/text-freetype2.plugin/Contents/MacOS/text-freetype2"
      SG_TEXT_MODULE_DATA="${SG_OBS_APP}/Contents/PlugIns/text-freetype2.plugin/Contents/Resources"
      SG_SNAPSHOT_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(sg_obs_monitor_test obs-soundgrid-input)
    add_test(NAME OBS_Monitor_Render COMMAND sg_obs_monitor_test)
    set_tests_properties(OBS_Monitor_Render PROPERTIES LABELS "obs" TIMEOUT 120
      PASS_REGULAR_EXPRESSION "RESULTADO: PASS")
  endif()
endif()
