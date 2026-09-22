cmake_minimum_required(VERSION 3.16)

project(Water1_MapViewer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(Water1_MapViewer
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/SourceDirectory.cpp
    src/SourceDirectory.h
    src/WorldMapGenerator.cpp
    src/WorldMapGenerator.h
    src/MapCanvas.cpp
    src/MapCanvas.h
    src/MapPackage.cpp
    src/MapPackage.h
    src/TilePyramidItem.cpp
    src/TilePyramidItem.h
    src/MapDataOverlayItem.cpp
    src/MapDataOverlayItem.h
    src/SaveDataReader.cpp
    src/SaveDataReader.h
)

target_link_libraries(Water1_MapViewer PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)

target_compile_options(Water1_MapViewer PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS Water1_MapViewer RUNTIME DESTINATION bin)

include(CTest)
option(WATER1_MAPVIEWER_ENABLE_BENCHMARKS "Register the memory-intensive baseline generator benchmark" OFF)
if(BUILD_TESTING)
    find_package(Qt5 5.12 REQUIRED COMPONENTS Test)
    add_executable(Water1_MapViewerTests
        tests/MapViewerTests.cpp
        src/SourceDirectory.cpp
        src/SourceDirectory.h
        src/WorldMapGenerator.cpp
        src/WorldMapGenerator.h
        src/MainWindow.cpp
        src/MainWindow.h
        src/MapCanvas.cpp
        src/MapCanvas.h
        src/MapPackage.cpp
        src/MapPackage.h
        src/TilePyramidItem.cpp
        src/TilePyramidItem.h
        src/MapDataOverlayItem.cpp
        src/MapDataOverlayItem.h
        src/SaveDataReader.cpp
        src/SaveDataReader.h
    )
    target_include_directories(Water1_MapViewerTests PRIVATE src)
    target_compile_definitions(Water1_MapViewerTests PRIVATE
        WATER1_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../water1eng"
        WATER1_TEST_PACKAGE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/output/uw1-eng-dos-uw1-eng-dos-baseline/world_map.json"
    )
    target_link_libraries(Water1_MapViewerTests PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Test)
    target_compile_options(Water1_MapViewerTests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME Water1_MapViewerTests COMMAND Water1_MapViewerTests)
    set_tests_properties(Water1_MapViewerTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME Water1_PackageAnalysis
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_package.py
                    --source ${CMAKE_CURRENT_SOURCE_DIR}/../water1eng
                    --package ${CMAKE_CURRENT_SOURCE_DIR}/output/uw1-eng-dos-uw1-eng-dos-baseline)
        set_tests_properties(Water1_PackageAnalysis PROPERTIES SKIP_RETURN_CODE 77)
        if(WATER1_MAPVIEWER_ENABLE_BENCHMARKS)
            add_test(NAME Water1_GenerationBenchmark
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_generation.py
                        --binary $<TARGET_FILE:Water1_MapViewer>
                        --source ${CMAKE_CURRENT_SOURCE_DIR}/../water1eng)
        endif()
    endif()
endif()
