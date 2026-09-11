cmake_minimum_required(VERSION 3.27)

project(NarrationStudio
  VERSION 0.2.0
  DESCRIPTION "Fast manga/webtoon narration studio (C++/Qt port)"
  LANGUAGES CXX
)
add_compile_definitions(NS_APP_VERSION="${PROJECT_VERSION}")
# Lets runtime file lookup fall back to the source tree, so running the exe
# straight from build/release still finds models/ + third_party/ in dev.
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" NS_SRC_DIR_FWD)
add_compile_definitions(NS_SOURCE_DIR="${NS_SRC_DIR_FWD}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Shareability: core app builds with Qt only. Heavy AI backends are opt-in
# so a clean clone + Qt 6.8 builds without 1.35GB of models.
option(ENABLE_AI_BACKENDS "Build ONNX/libtorch AI backends (needs vcpkg torch+onnx)" OFF)
option(BUILD_TESTS "Build CTest unit tests" ON)
option(ENABLE_KOKORO "Build Kokoro TTS voice (needs third_party/onnxruntime)" ON)

set(ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
if(ENABLE_KOKORO AND EXISTS "${ORT_ROOT}/include/onnxruntime_cxx_api.h")
  set(NS_HAS_KOKORO TRUE)
  message(STATUS "Kokoro TTS ON (ORT ${ORT_ROOT})")
else()
  set(NS_HAS_KOKORO FALSE)
  message(STATUS "Kokoro TTS OFF (run scripts/fetch-kokoro.ps1 for voice files)")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS
  Widgets Network Sql Multimedia MultimediaWidgets Concurrent Svg Pdf PdfWidgets
)

set(APP_SOURCES
  src/main.cpp
  src/core/pipeline.cpp src/core/pipeline.h
  src/core/timeline.cpp src/core/timeline.h
  src/core/settings.cpp src/core/settings.h
  src/core/project.cpp src/core/project.h
  src/services/ocr_service.cpp src/services/ocr_service.h
  src/services/caption_service.cpp src/services/caption_service.h
  src/services/ai_service.cpp src/services/ai_service.h
  src/services/engines.h
  src/services/stubs.cpp
  src/services/tts_service.cpp src/services/tts_service.h
  src/services/gemini_describe.cpp src/services/gemini_describe.h
  src/services/video_service.cpp src/services/video_service.h
  src/services/mangadx_service.cpp src/services/mangadx_service.h
  src/services/webtoon_service.cpp src/services/webtoon_service.h
  src/services/kokoro_npz.cpp src/services/kokoro_npz.h
  src/services/kokoro_g2p.cpp src/services/kokoro_g2p.h
  src/services/misaki_lite.cpp src/services/misaki_lite.h
  src/services/kokoro_paths.cpp src/services/kokoro_engine.h
  src/services/kokoro_voices.h
  $<$<BOOL:${NS_HAS_KOKORO}>:src/services/kokoro_engine.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:src/services/kokoro_fallback.cpp>
  src/services/rapid_ocr_units.cpp src/services/rapid_ocr.h
  $<$<BOOL:${NS_HAS_KOKORO}>:src/services/rapid_ocr.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:src/services/rapid_ocr_fallback.cpp>
  src/services/florence_tok.cpp src/services/florence_tok.h
  src/services/florence_engine.h
  $<$<BOOL:${NS_HAS_KOKORO}>:src/services/florence_engine.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:src/services/florence_fallback.cpp>
  src/services/narrate.cpp src/services/narrate.h
  src/database/database.cpp src/database/database.h
  src/database/projects.cpp src/database/projects.h
  src/ui/main_window.cpp src/ui/main_window.h
  src/ui/glass.cpp src/ui/glass.h
  src/ui/timeline_widget.cpp src/ui/timeline_widget.h
  src/ui/dialogs.cpp src/ui/dialogs.h
  src/ui/theme.cpp src/ui/theme.h
  src/ui/workers.cpp src/ui/workers.h
  resources/app.qrc
)

qt_add_executable(NarrationStudio MANUAL_FINALIZATION ${APP_SOURCES})

target_include_directories(NarrationStudio PRIVATE src)

target_link_libraries(NarrationStudio PRIVATE
  Qt6::Widgets
  Qt6::Network
  Qt6::Sql
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  Qt6::Concurrent
  Qt6::Svg
  Qt6::Pdf
  Qt6::PdfWidgets
)

if(NS_HAS_KOKORO)
  target_include_directories(NarrationStudio PRIVATE "${ORT_ROOT}/include")
  target_link_libraries(NarrationStudio PRIVATE "${ORT_ROOT}/lib/onnxruntime.lib")
  target_compile_definitions(NarrationStudio PRIVATE NS_HAS_KOKORO=1)
  # Runtime next to the exe: ORT + espeak + voice files
  add_custom_command(TARGET NarrationStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:NarrationStudio>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/third_party/espeak-ng/espeak-ng.dll" "$<TARGET_FILE_DIR:NarrationStudio>"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/third_party/espeak-ng/espeak-ng-data" "$<TARGET_FILE_DIR:NarrationStudio>/espeak-ng-data"
  )
endif()
# Bundled ffmpeg next to the exe (fetch-ffmpeg.ps1); skipped if not downloaded.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/ffmpeg.exe")
  add_custom_command(TARGET NarrationStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/ffmpeg.exe" "$<TARGET_FILE_DIR:NarrationStudio>"
  )
  install(FILES "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/ffmpeg.exe"
    DESTINATION bin OPTIONAL)
endif()

if(ENABLE_AI_BACKENDS)
  message(STATUS "AI backends ON: looking for onnxruntime + torch")
  find_package(onnxruntime CONFIG QUIET)
  find_package(Torch QUIET)
  if(onnxruntime_FOUND)
    target_link_libraries(NarrationStudio PRIVATE onnxruntime::onnxruntime)
    target_compile_definitions(NarrationStudio PRIVATE NS_HAS_ONNX=1)
  endif()
  if(Torch_FOUND)
    target_link_libraries(NarrationStudio PRIVATE "${TORCH_LIBRARIES}")
    target_compile_definitions(NarrationStudio PRIVATE NS_HAS_TORCH=1)
  endif()
endif()

set_target_properties(NarrationStudio PROPERTIES
  WIN32_EXECUTABLE TRUE
  MACOSX_BUNDLE TRUE
)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- Install + distribution (easy shareability) ----
# `cmake --build --preset release && cpack -G NSIS/ZIP` gives installer + portable zip.
install(TARGETS NarrationStudio
  BUNDLE DESTINATION .
  RUNTIME DESTINATION bin
)
install(DIRECTORY resources/models-readme/
  DESTINATION share/models OPTIONAL
)
if(NS_HAS_KOKORO)
  install(FILES "${ORT_ROOT}/lib/onnxruntime.dll"
    DESTINATION bin OPTIONAL)
  install(FILES "${CMAKE_SOURCE_DIR}/third_party/espeak-ng/espeak-ng.dll"
    DESTINATION bin OPTIONAL)
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/espeak-ng/espeak-ng-data"
    DESTINATION bin OPTIONAL)
  # Full-precision model only (int8 fallback stays a dev download, not shipped).
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/models/kokoro"
    DESTINATION share/models OPTIONAL
    FILES_MATCHING PATTERN "*.onnx" PATTERN "*.bin" PATTERN "*.json"
    PATTERN "kokoro-v1.0.int8.onnx" EXCLUDE)
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/misaki"
    DESTINATION share/models/misaki OPTIONAL
    FILES_MATCHING PATTERN "*.json")
endif()
# OCR + Florence models ride along when downloaded (fetch-*.ps1).
install(DIRECTORY "${CMAKE_SOURCE_DIR}/models/ocr"
  DESTINATION share/models OPTIONAL
  FILES_MATCHING PATTERN "*.onnx")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/models/florence"
  DESTINATION share/models OPTIONAL
  FILES_MATCHING PATTERN "*.onnx" PATTERN "*.json")
install(FILES README.md ARCHITECTURE_CPP.md
  DESTINATION share/doc
)

# Qt runtime next to the installed exe at packaging time (replaces the
# manual windeployqt step for anything shipped via CPack).
qt_generate_deploy_app_script(
  TARGET NarrationStudio
  OUTPUT_SCRIPT NS_DEPLOY_SCRIPT
  NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${NS_DEPLOY_SCRIPT})

set(CPACK_PACKAGE_NAME "NarrationStudio")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "NarrationStudio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast manga narration studio")
set(CPACK_GENERATOR "ZIP;NSIS")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
include(CPack)

qt_finalize_executable(NarrationStudio)
