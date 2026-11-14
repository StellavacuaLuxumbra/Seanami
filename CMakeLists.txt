cmake_minimum_required(VERSION 3.16)
project(Seanami VERSION 0.2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # 与官方 Python 运行时保持一致，使用动态 CRT (/MD)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    add_compile_options(/W4 /EHsc /utf-8)
endif()

add_executable(seanami
    src/main.cpp
    src/cli/args.cpp
    src/cli/progress.cpp
    src/app/transcribe_job.cpp
    src/app/batch_runner.cpp
    src/infra/audio_decoder.cpp
    src/infra/python_transcriber.cpp
    src/infra/midi_validator.cpp
)

target_include_directories(seanami PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

find_package(Threads REQUIRED)
target_link_libraries(seanami PRIVATE Threads::Threads)

if(WIN32)
    target_link_libraries(seanami PRIVATE shell32)
endif()

# 把脚本和模型放到构建目录旁边，方便本地测试
add_custom_command(TARGET seanami POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:seanami>/scripts
)
