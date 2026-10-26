cmake_minimum_required(VERSION 3.27)
project(QtFFmpegStudio)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        REQUIRED)

include(CTest)

set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../FFmpegProject/ffmpeg-4.4-full_build-shared"
        CACHE PATH "FFmpeg 4.4 shared SDK")
configure_file("${FFMPEG_ROOT}/bin/ffprobe.exe" "${CMAKE_BINARY_DIR}/ffprobe.exe" COPYONLY)

set(BACKEND_SOURCES
        backend/main.cpp
        backend/src/safe_output.cpp
        backend/src/transcode.cpp
        backend/src/thread_pool.cpp
        backend/src/pipeline.cpp
        backend/src/common.cpp
        backend/src/ring_buffer.cpp
        backend/src/demux.cpp
        backend/src/videodecoder.cpp
        backend/src/audiodecoder.cpp
        backend/src/videoencoder.cpp
        backend/src/audioencoder.cpp
        backend/src/mux.cpp
        backend/src/video_rotate.cpp
        backend/src/logger.cpp
)

add_executable(FFmpegProjectBackend ${BACKEND_SOURCES})
set_target_properties(FFmpegProjectBackend PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(FFmpegProjectBackend PRIVATE
        backend/include
        "${FFMPEG_ROOT}/include"
)
target_link_libraries(FFmpegProjectBackend PRIVATE
        "${FFMPEG_ROOT}/lib/libavcodec.dll.a"
        "${FFMPEG_ROOT}/lib/libavdevice.dll.a"
        "${FFMPEG_ROOT}/lib/libavfilter.dll.a"
        "${FFMPEG_ROOT}/lib/libavformat.dll.a"
        "${FFMPEG_ROOT}/lib/libavutil.dll.a"
        "${FFMPEG_ROOT}/lib/libpostproc.dll.a"
        "${FFMPEG_ROOT}/lib/libswresample.dll.a"
        "${FFMPEG_ROOT}/lib/libswscale.dll.a"
)
target_link_options(FFmpegProjectBackend PRIVATE -municode)
file(GLOB FFMPEG_RUNTIME_DLLS "${FFMPEG_ROOT}/bin/*.dll")
add_custom_command(TARGET FFmpegProjectBackend POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FFMPEG_RUNTIME_DLLS} "$<TARGET_FILE_DIR:FFmpegProjectBackend>"
        COMMAND_EXPAND_LISTS)
if (MINGW)
    get_filename_component(MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET FFmpegProjectBackend POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_RUNTIME_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_RUNTIME_DIR}/libstdc++-6.dll"
            "${MINGW_RUNTIME_DIR}/libwinpthread-1.dll"
            "$<TARGET_FILE_DIR:FFmpegProjectBackend>")
endif ()

add_executable(QtFFmpegStudio
        main.cpp
        src/mainwindow.h
        src/mainwindow.cpp
        src/mediaprobe.h
        src/mediaprobe.cpp
        src/transcodeoptions.h
        src/backendprocess.h
        src/backendprocess.cpp
        src/batchtaskmodel.h
        src/batchtaskmodel.cpp
        src/batchwidget.h
        src/batchwidget.cpp
)
target_include_directories(QtFFmpegStudio PRIVATE src)
target_link_libraries(QtFFmpegStudio
        Qt::Core
        Qt::Gui
        Qt::Widgets
)
add_dependencies(QtFFmpegStudio FFmpegProjectBackend)

if (BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(QtFFmpegStudio_options_tests
            tests/test_options.cpp
            src/mainwindow.h
            src/mainwindow.cpp
            src/mediaprobe.h
            src/mediaprobe.cpp
            src/backendprocess.h
            src/backendprocess.cpp
            src/batchtaskmodel.h
            src/batchtaskmodel.cpp
            src/batchwidget.h
            src/batchwidget.cpp
    )
    target_include_directories(QtFFmpegStudio_options_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_options_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME options_preview COMMAND QtFFmpegStudio_options_tests)
    set_tests_properties(options_preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(QtFFmpegStudio_simulation_tests
            tests/test_simulation.cpp
            src/mainwindow.h
            src/mainwindow.cpp
            src/mediaprobe.h
            src/mediaprobe.cpp
            src/backendprocess.h
            src/backendprocess.cpp
            src/batchtaskmodel.h
            src/batchtaskmodel.cpp
            src/batchwidget.h
            src/batchwidget.cpp
    )
    target_include_directories(QtFFmpegStudio_simulation_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_simulation_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME simulated_task COMMAND QtFFmpegStudio_simulation_tests)
    set_tests_properties(simulated_task PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(QtFFmpegStudio_real_tests
            tests/test_real_transcode.cpp
            src/mainwindow.h
            src/mainwindow.cpp
            src/mediaprobe.h
            src/mediaprobe.cpp
            src/backendprocess.h
            src/backendprocess.cpp
            src/batchtaskmodel.h
            src/batchtaskmodel.cpp
            src/batchwidget.h
            src/batchwidget.cpp
    )
    target_include_directories(QtFFmpegStudio_real_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_real_tests PRIVATE Qt6::Widgets Qt6::Test)
    target_compile_definitions(QtFFmpegStudio_real_tests PRIVATE
            FFMPEG_TEST_TOOL="${FFMPEG_ROOT}/bin/ffmpeg.exe")
    add_dependencies(QtFFmpegStudio_real_tests FFmpegProjectBackend)
    add_test(NAME real_transcode COMMAND QtFFmpegStudio_real_tests)
    set_tests_properties(real_transcode PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(QtFFmpegStudio_engineering_tests
            tests/test_engineering.cpp
            src/mainwindow.h
            src/mainwindow.cpp
            src/mediaprobe.h
            src/mediaprobe.cpp
            src/backendprocess.h
            src/backendprocess.cpp
            src/batchtaskmodel.h
            src/batchtaskmodel.cpp
            src/batchwidget.h
            src/batchwidget.cpp
    )
    target_include_directories(QtFFmpegStudio_engineering_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_engineering_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME engineering_settings COMMAND QtFFmpegStudio_engineering_tests)
    set_tests_properties(engineering_settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(QtFFmpegStudio_batch_model_tests
            tests/test_batch_model.cpp
            src/batchtaskmodel.h
            src/batchtaskmodel.cpp)
    target_include_directories(QtFFmpegStudio_batch_model_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_batch_model_tests PRIVATE Qt6::Widgets Qt6::Test)
    add_test(NAME batch_model COMMAND QtFFmpegStudio_batch_model_tests)
    set_tests_properties(batch_model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(QtFFmpegStudio_batch_widget_tests
            tests/test_batch_widget.cpp
            src/batchwidget.h
            src/batchwidget.cpp
            src/batchtaskmodel.h
            src/batchtaskmodel.cpp
            src/backendprocess.h
            src/backendprocess.cpp)
    target_include_directories(QtFFmpegStudio_batch_widget_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_batch_widget_tests PRIVATE Qt6::Widgets Qt6::Test)
    target_compile_definitions(QtFFmpegStudio_batch_widget_tests PRIVATE
            FFMPEG_TEST_TOOL="${FFMPEG_ROOT}/bin/ffmpeg.exe")
    add_dependencies(QtFFmpegStudio_batch_widget_tests FFmpegProjectBackend)
    add_test(NAME batch_queue COMMAND QtFFmpegStudio_batch_widget_tests)
    set_tests_properties(batch_queue PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(QtFFmpegStudio_batch_integration_tests
            tests/test_batch_integration.cpp
            src/mainwindow.h
            src/mainwindow.cpp
            src/mediaprobe.h
            src/mediaprobe.cpp
            src/batchwidget.h
            src/batchwidget.cpp
            src/batchtaskmodel.h
            src/batchtaskmodel.cpp
            src/backendprocess.h
            src/backendprocess.cpp)
    target_include_directories(QtFFmpegStudio_batch_integration_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_batch_integration_tests PRIVATE Qt6::Widgets Qt6::Test)
    target_compile_definitions(QtFFmpegStudio_batch_integration_tests PRIVATE
            FFMPEG_TEST_TOOL="${FFMPEG_ROOT}/bin/ffmpeg.exe")
    add_dependencies(QtFFmpegStudio_batch_integration_tests FFmpegProjectBackend)
    add_test(NAME batch_integration COMMAND QtFFmpegStudio_batch_integration_tests)
    set_tests_properties(batch_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(QtFFmpegStudio_media_probe_tests
            tests/test_media_probe.cpp
            src/mainwindow.h src/mainwindow.cpp
            src/mediaprobe.h src/mediaprobe.cpp
            src/backendprocess.h src/backendprocess.cpp
            src/batchtaskmodel.h src/batchtaskmodel.cpp
            src/batchwidget.h src/batchwidget.cpp)
    target_include_directories(QtFFmpegStudio_media_probe_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_media_probe_tests PRIVATE Qt6::Widgets Qt6::Test)
    target_compile_definitions(QtFFmpegStudio_media_probe_tests PRIVATE
            FFMPEG_TEST_TOOL="${FFMPEG_ROOT}/bin/ffmpeg.exe")
    add_dependencies(QtFFmpegStudio_media_probe_tests FFmpegProjectBackend)
    add_test(NAME media_probe COMMAND QtFFmpegStudio_media_probe_tests)
    set_tests_properties(media_probe PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(QtFFmpegStudio_safe_output_tests
            tests/test_safe_output.cpp
            backend/include/safe_output.h
            backend/src/safe_output.cpp)
    target_include_directories(QtFFmpegStudio_safe_output_tests PRIVATE backend/include)
    add_test(NAME safe_output COMMAND QtFFmpegStudio_safe_output_tests)
    add_executable(QtFFmpegStudio_mux_progress_tests
            tests/test_mux_progress.cpp
            backend/include/mux_progress.h)
    target_include_directories(QtFFmpegStudio_mux_progress_tests PRIVATE backend/include)
    add_test(NAME mux_progress COMMAND QtFFmpegStudio_mux_progress_tests)
    add_executable(QtFFmpegStudio_transcode_progress_parser_tests
            tests/test_transcode_progress_parser.cpp
            src/transcodeprogressparser.h)
    target_include_directories(QtFFmpegStudio_transcode_progress_parser_tests PRIVATE src)
    target_link_libraries(QtFFmpegStudio_transcode_progress_parser_tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME transcode_progress_parser COMMAND QtFFmpegStudio_transcode_progress_parser_tests)
    if (WIN32)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test-tmp")
        set(TEST_ENVIRONMENT_MODIFICATIONS
                "PATH=path_list_prepend:${CMAKE_PREFIX_PATH}/bin"
                "TEMP=set:${CMAKE_BINARY_DIR}/test-tmp"
                "TMP=set:${CMAKE_BINARY_DIR}/test-tmp")
        if (MINGW)
            list(APPEND TEST_ENVIRONMENT_MODIFICATIONS
                    "PATH=path_list_prepend:${MINGW_RUNTIME_DIR}")
        endif ()
        set_tests_properties(options_preview simulated_task real_transcode media_probe
                engineering_settings batch_model batch_queue batch_integration safe_output
                transcode_progress_parser mux_progress
                PROPERTIES ENVIRONMENT_MODIFICATION "${TEST_ENVIRONMENT_MODIFICATIONS}")
    endif ()
endif ()

if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif ()
    foreach (QT_LIB Core Gui Widgets)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach (QT_LIB)
endif ()
