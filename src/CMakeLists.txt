# --------------------------------------------------------------------------- núcleo
# sg_core no depende de libobs ni de Qt: se compila y prueba en CI sin OBS ni hardware.
add_library(sg_core STATIC
  common/Clock.h common/Types.h common/Common.cpp common/Settings.h common/Settings.cpp
  audio/SpscQueue.h audio/PcmBlockRing.h audio/SampleConvert.h audio/SampleConvert.cpp
  audio/ChannelMap.h audio/ChannelMap.cpp audio/Metering.h audio/TimestampSmoother.h
  transport/IAudioTransport.h transport/MockTransport.h transport/MockTransport.cpp
  transport/DeviceHub.h transport/DeviceHub.cpp
  transport/TransportManager.h transport/TransportManager.cpp
  platform/INetworkTelemetry.h platform/WavesDriverPrefs.h platform/WavesDriverPrefs.cpp
  platform/WavesCompanion.h platform/WavesCompanion.cpp
  observability/HealthSnapshot.h observability/AlertTypes.h
  observability/AudioLossBanner.h observability/AudioLossBanner.cpp
  observability/AlertEngine.h observability/AlertEngine.cpp
  observability/AlertSink.h observability/AlertSink.cpp
  core/SourceSession.h core/SourceSession.cpp
)
target_include_directories(sg_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(sg_core PUBLIC cxx_std_20)
find_package(Threads REQUIRED)
target_link_libraries(sg_core PUBLIC Threads::Threads)

if(WIN32)
  target_sources(sg_core PRIVATE platform/windows/WindowsNicTelemetry.cpp platform/windows/WavesCompanionWin.cpp)
  target_link_libraries(sg_core PUBLIC iphlpapi ole32)
  if(SG_ENABLE_ASIO)
    target_sources(sg_core PRIVATE platform/windows/AsioTransport.h platform/windows/AsioTransport.cpp)
    target_compile_definitions(sg_core PUBLIC SG_HAVE_ASIO=1)
  endif()
elseif(APPLE)
  target_sources(sg_core PRIVATE platform/macos/MacNicTelemetry.cpp platform/WavesCompanionNull.cpp)
  if(SG_ENABLE_COREAUDIO)
    target_sources(sg_core PRIVATE platform/macos/CoreAudioTransport.h platform/macos/CoreAudioTransport.cpp)
    target_compile_definitions(sg_core PUBLIC SG_HAVE_COREAUDIO=1)
    target_link_libraries(sg_core PUBLIC "-framework CoreAudio" "-framework CoreFoundation")
  endif()
else()
  target_sources(sg_core PRIVATE platform/NullNicTelemetry.cpp platform/WavesCompanionNull.cpp)
endif()

# --------------------------------------------------------------------------- plugin OBS
if(SG_BUILD_OBS_PLUGIN)
  add_subdirectory(obs)
endif()
