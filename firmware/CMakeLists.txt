cmake_minimum_required(VERSION 3.16)

# Application code lives under components/. Third-party dependencies are
# resolved from main/idf_component.yml by the IDF component manager.
set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(star_pet)
