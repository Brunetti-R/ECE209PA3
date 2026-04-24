# File: CMakeLists.txt
# Project: The Greatest Game Calculator
# AI Assistance Disclosure:
# This file includes code developed with assistance from OpenAI Codex (GPT-5).
# All generated content was reviewed and integrated by the project author.

cmake_minimum_required(VERSION 4.1)
project(ece209pa3 C)

set(CMAKE_C_STANDARD 17)

add_executable(ece209pa3 main.c
        processing.c
        miniz-3.1.1/miniz.c)

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data.zip")
    add_custom_command(TARGET ece209pa3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data.zip"
            "$<TARGET_FILE_DIR:ece209pa3>/data.zip")
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test.zip")
    # Local-dev convenience: allow template test data while program expects data.zip.
    add_custom_command(TARGET ece209pa3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/test.zip"
            "$<TARGET_FILE_DIR:ece209pa3>/data.zip")
endif ()
