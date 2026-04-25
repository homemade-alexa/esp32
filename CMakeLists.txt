cmake_minimum_required(VERSION 3.5)

set(COMPONENTS main)

if(DEFINED ENV{ALEXA_VERSION})
    add_definitions(-DWILLOW_USER_AGENT="Alexa/$ENV{ALEXA_VERSION}")
else()
    add_definitions(-DWILLOW_USER_AGENT="Alexa/0.1")
endif()

include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(alexa)
