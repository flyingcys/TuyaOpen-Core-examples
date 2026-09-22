cmake_minimum_required(VERSION 3.16)

# 组件在工程 components/ 下，esp-idf 自动发现，无需 EXTRA_COMPONENT_DIRS
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(switch_demo)
