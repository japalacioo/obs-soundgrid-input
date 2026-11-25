include(FetchContent)
# GoogleTest desde la release oficial (github.com/google/googletest).
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
  URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(sg_unit_tests
  unit/test_audio.cpp
  unit/test_transport.cpp
  unit/test_alerts.cpp
  unit/test_sinks_settings.cpp
  unit/test_session.cpp
  unit/test_realtime.cpp
  unit/test_review_fixes.cpp
  unit/test_banner.cpp
  unit/test_waves_companion.cpp
  unit/test_audio_monitor.cpp
  unit/test_loudness.cpp
  unit/test_loudness_view.cpp
  unit/TestHelpers.h
)
target_link_libraries(sg_unit_tests PRIVATE sg_core GTest::gtest_main)

add_executable(sg_platform_tests integration/test_platform_local.cpp)
target_link_libraries(sg_platform_tests PRIVATE sg_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(sg_unit_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS "unit")
# Pruebas contra la NIC y los drivers reales de la máquina (sin LV1). Informativas.
gtest_discover_tests(sg_platform_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS "platform")

# Sonda HIL de solo lectura (no es una prueba de CTest): sg_asio_probe SoundGrid 10
if(WIN32 AND SG_ENABLE_ASIO)
  add_executable(sg_asio_probe hil/sg_asio_probe.cpp)
  target_link_libraries(sg_asio_probe PRIVATE sg_core)
endif()

# Resistencia (Fase 5). En CTest corre 15 s; para la prueba larga: sg_soak 120
add_executable(sg_soak soak/soak_main.cpp)
target_link_libraries(sg_soak PRIVATE sg_core)
if(WIN32)
  target_link_libraries(sg_soak PRIVATE psapi)
endif()
add_test(NAME Soak_Short COMMAND sg_soak 0.25 128 32 4 0)
set_tests_properties(Soak_Short PROPERTIES LABELS "soak" TIMEOUT 120 PASS_REGULAR_EXPRESSION "RESULTADO: PASS")
