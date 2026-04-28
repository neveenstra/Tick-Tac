cmake_minimum_required(VERSION 3.16)
project(tick_tac C)

# Add LVGL as a subdirectory
# If LVGL is installed as a dependency, use find_package instead
add_subdirectory(lvgl)

# Create the executable
add_executable(tick_tac src/main.c)

# Link LVGL library
target_link_libraries(tick_tac PRIVATE lvgl)

# Include directories
target_include_directories(tick_tac PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
