cmake_minimum_required(VERSION 3.20)
project(speakinto LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(speakinto WIN32
  src/main.cpp
  src/keyboard_hook.cpp
  src/audio_capture.cpp
  src/transcriber.cpp
  src/text_injector.cpp
  src/wav_writer.cpp
  src/tray.cpp
  src/overlay.cpp
  src/model_manager.cpp
  src/settings.cpp
  src/processor.cpp
  src/processor_llm.cpp
  src/updater.cpp
  src/cuda_manager.cpp
)

target_link_libraries(speakinto PRIVATE
  ole32
  uuid
  user32
  shell32
  gdi32
  winhttp
)

target_compile_definitions(speakinto PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
