cmake_minimum_required(VERSION 3.10)
project(mygl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(glad STATIC external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)

add_subdirectory(external/glfw)
add_subdirectory(external/glm)

add_executable(mygl src/main.cpp)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)

target_sources(mygl PRIVATE
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp

    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    external/ImGuizmo/ImGuizmo.cpp
)

target_include_directories(mygl PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    external/ImGuizmo
)

target_link_libraries(mygl PRIVATE
  glad
  glfw
  glm::glm
)

if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(mygl PRIVATE X11::X11)
endif()

add_custom_command(TARGET mygl POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets
          $<TARGET_FILE_DIR:mygl>/assets
)
