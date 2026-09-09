cmake_minimum_required(VERSION 3.27)
find_package(Qt6 6.8 REQUIRED COMPONENTS Test Widgets Network Sql Pdf Concurrent)

function(ns_test name src)
  qt_add_executable(${name} ${src})
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(${name} PRIVATE Qt6::Test
    $<$<BOOL:1>:> # sources compiled in per-test below
  )
endfunction()

# Timeline math test (compiles timeline.cpp directly — no QtWidgets needed)
qt_add_executable(t_timeline t_timeline.cpp
  ${CMAKE_SOURCE_DIR}/src/core/timeline.cpp
)
target_include_directories(t_timeline PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(t_timeline PRIVATE Qt6::Test)
add_test(NAME timeline COMMAND t_timeline)

# Narration prompt/tag-parser/tactics test (+ headless net probe)
qt_add_executable(t_narration t_narration.cpp
  ${CMAKE_SOURCE_DIR}/src/services/engines.h
  ${CMAKE_SOURCE_DIR}/src/services/stubs.cpp
  ${CMAKE_SOURCE_DIR}/src/services/tts_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/tts_service.h
  ${CMAKE_SOURCE_DIR}/src/services/ai_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/narrate.cpp
  ${CMAKE_SOURCE_DIR}/src/core/timeline.cpp
)
target_include_directories(t_narration PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(t_narration PRIVATE Qt6::Test Qt6::Network)
add_test(NAME narration COMMAND t_narration)

# Kokoro offline pieces: vocab filter, npz reader, splitter, espeak smoke.
# (Needs models/third_party present; tests SKIP gracefully without them.)
qt_add_executable(t_kokoro t_kokoro.cpp
  ${CMAKE_SOURCE_DIR}/src/services/engines.h
  ${CMAKE_SOURCE_DIR}/src/services/stubs.cpp
  ${CMAKE_SOURCE_DIR}/src/services/kokoro_npz.cpp
  ${CMAKE_SOURCE_DIR}/src/services/kokoro_g2p.cpp
  ${CMAKE_SOURCE_DIR}/src/services/misaki_lite.cpp
  ${CMAKE_SOURCE_DIR}/src/services/kokoro_paths.cpp
  ${CMAKE_SOURCE_DIR}/src/core/timeline.cpp
  $<$<BOOL:${NS_HAS_KOKORO}>:${CMAKE_SOURCE_DIR}/src/services/kokoro_engine.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:${CMAKE_SOURCE_DIR}/src/services/kokoro_fallback.cpp>
)
target_include_directories(t_kokoro PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NS_HAS_KOKORO)
  target_include_directories(t_kokoro PRIVATE "${ORT_ROOT}/include")
  target_link_libraries(t_kokoro PRIVATE Qt6::Test "${ORT_ROOT}/lib/onnxruntime.lib")
  # Test exe lives in tests/ — real ORT dlls must sit beside it, else the
  # Windows inbox onnxruntime.dll stub in System32 shadows them (0xC000007B).
  add_custom_command(TARGET t_kokoro POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:t_kokoro>"
  )
else()
  target_link_libraries(t_kokoro PRIVATE Qt6::Test)
endif()
add_test(NAME kokoro COMMAND t_kokoro WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# OCR units (ctc + reading order) + live engine smoke (SKIPs without models).
qt_add_executable(t_ocr t_ocr.cpp
  ${CMAKE_SOURCE_DIR}/src/services/engines.h
  ${CMAKE_SOURCE_DIR}/src/services/stubs.cpp
  ${CMAKE_SOURCE_DIR}/src/services/rapid_ocr_units.cpp
  $<$<BOOL:${NS_HAS_KOKORO}>:${CMAKE_SOURCE_DIR}/src/services/rapid_ocr.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:${CMAKE_SOURCE_DIR}/src/services/rapid_ocr_fallback.cpp>
)
target_include_directories(t_ocr PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(t_ocr PRIVATE Qt6::Test Qt6::Gui)
if(NS_HAS_KOKORO)
  target_include_directories(t_ocr PRIVATE "${ORT_ROOT}/include")
  target_link_libraries(t_ocr PRIVATE "${ORT_ROOT}/lib/onnxruntime.lib")
  add_custom_command(TARGET t_ocr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:t_ocr>"
  )
else()
  target_link_libraries(t_ocr PRIVATE Qt6::Test)
endif()
add_test(NAME ocr COMMAND t_ocr WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Florence BPE round-trip + live caption smoke (SKIPs without models).
qt_add_executable(t_florence t_florence.cpp
  ${CMAKE_SOURCE_DIR}/src/services/engines.h
  ${CMAKE_SOURCE_DIR}/src/services/stubs.cpp
  ${CMAKE_SOURCE_DIR}/src/services/florence_tok.cpp
  $<$<BOOL:${NS_HAS_KOKORO}>:${CMAKE_SOURCE_DIR}/src/services/florence_engine.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:${CMAKE_SOURCE_DIR}/src/services/florence_fallback.cpp>
)
target_include_directories(t_florence PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NS_HAS_KOKORO)
  target_include_directories(t_florence PRIVATE "${ORT_ROOT}/include")
  target_link_libraries(t_florence PRIVATE Qt6::Test Qt6::Gui "${ORT_ROOT}/lib/onnxruntime.lib")
  add_custom_command(TARGET t_florence POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:t_florence>"
  )
else()
  target_link_libraries(t_florence PRIVATE Qt6::Test)
endif()
add_test(NAME florence COMMAND t_florence WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# SQLite round-trip (save/load parity incl. cast + work range).
qt_add_executable(t_database t_database.cpp
  ${CMAKE_SOURCE_DIR}/src/database/database.cpp
  ${CMAKE_SOURCE_DIR}/src/database/projects.cpp
  ${CMAKE_SOURCE_DIR}/src/core/project.cpp
)
target_include_directories(t_database PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(t_database PRIVATE Qt6::Test Qt6::Sql)
add_test(NAME database COMMAND t_database WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Video export smoke (needs ffmpeg; SKIPs without it).
qt_add_executable(t_video t_video.cpp
  ${CMAKE_SOURCE_DIR}/src/services/video_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/video_service.h
)
target_include_directories(t_video PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(t_video PRIVATE Qt6::Test Qt6::Gui Qt6::Concurrent)
add_test(NAME video COMMAND t_video WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Offscreen UI regression (friend-driven MainWindow). Needs temp/wtprobe
# images + OCR models; SKIPs gracefully without them.
qt_add_executable(t_uiprobe t_uiprobe.cpp
  ${CMAKE_SOURCE_DIR}/src/core/pipeline.cpp
  ${CMAKE_SOURCE_DIR}/src/core/timeline.cpp
  ${CMAKE_SOURCE_DIR}/src/core/settings.cpp
  ${CMAKE_SOURCE_DIR}/src/core/project.cpp
  ${CMAKE_SOURCE_DIR}/src/services/ocr_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/caption_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/ai_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/engines.h
  ${CMAKE_SOURCE_DIR}/src/services/stubs.cpp
  ${CMAKE_SOURCE_DIR}/src/services/tts_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/tts_service.h
  ${CMAKE_SOURCE_DIR}/src/services/video_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/mangadx_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/webtoon_service.cpp
  ${CMAKE_SOURCE_DIR}/src/services/kokoro_npz.cpp
  ${CMAKE_SOURCE_DIR}/src/services/kokoro_g2p.cpp
  ${CMAKE_SOURCE_DIR}/src/services/misaki_lite.cpp
  ${CMAKE_SOURCE_DIR}/src/services/kokoro_paths.cpp
  ${CMAKE_SOURCE_DIR}/src/services/rapid_ocr_units.cpp
  ${CMAKE_SOURCE_DIR}/src/services/florence_tok.cpp
  ${CMAKE_SOURCE_DIR}/src/services/narrate.cpp
  $<$<BOOL:${NS_HAS_KOKORO}>:${CMAKE_SOURCE_DIR}/src/services/kokoro_engine.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:${CMAKE_SOURCE_DIR}/src/services/kokoro_fallback.cpp>
  $<$<BOOL:${NS_HAS_KOKORO}>:${CMAKE_SOURCE_DIR}/src/services/rapid_ocr.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:${CMAKE_SOURCE_DIR}/src/services/rapid_ocr_fallback.cpp>
  $<$<BOOL:${NS_HAS_KOKORO}>:${CMAKE_SOURCE_DIR}/src/services/florence_engine.cpp>
  $<$<NOT:$<BOOL:${NS_HAS_KOKORO}>>:${CMAKE_SOURCE_DIR}/src/services/florence_fallback.cpp>
  ${CMAKE_SOURCE_DIR}/src/database/database.cpp
  ${CMAKE_SOURCE_DIR}/src/database/projects.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/main_window.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/main_window.h
  ${CMAKE_SOURCE_DIR}/src/ui/glass.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/timeline_widget.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/dialogs.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp
  ${CMAKE_SOURCE_DIR}/src/ui/workers.cpp
  ${CMAKE_SOURCE_DIR}/resources/app.qrc
)
target_include_directories(t_uiprobe PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(NS_HAS_KOKORO)
  target_include_directories(t_uiprobe PRIVATE "${ORT_ROOT}/include")
  set(UIPROBE_LIBS Qt6::Test Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Concurrent Qt6::Svg Qt6::Pdf Qt6::PdfWidgets "${ORT_ROOT}/lib/onnxruntime.lib")
else()
  set(UIPROBE_LIBS Qt6::Test Qt6::Widgets Qt6::Network Qt6::Sql Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Concurrent Qt6::Svg Qt6::Pdf Qt6::PdfWidgets)
endif()
target_link_libraries(t_uiprobe PRIVATE ${UIPROBE_LIBS})
if(NS_HAS_KOKORO)
  add_custom_command(TARGET t_uiprobe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ORT_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:t_uiprobe>"
  )
endif()
add_test(NAME uiprobe COMMAND t_uiprobe WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
