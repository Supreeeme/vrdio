cmake_minimum_required(VERSION 3.19.0)

project(vrdio VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# put exe in main directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
find_package(Qt6 COMPONENTS Core Gui Quick QuickControls2 REQUIRED)

file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/resources.qrc")

add_executable(vrdio ${SOURCES})
target_include_directories(vrdio PRIVATE ${PROJECT_SOURCE_DIR}/src openvr/headers)
target_link_directories(vrdio PRIVATE openvr/lib/linux64)
target_link_libraries(vrdio PRIVATE Qt6::Core Qt6::Quick Qt6::QuickControls2 Qt6::Gui openvr_api vulkan pulse)

