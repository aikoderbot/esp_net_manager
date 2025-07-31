# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Add the path to the rotary_button component
set(EXTRA_COMPONENT_DIRS "../")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(net_manager_example)