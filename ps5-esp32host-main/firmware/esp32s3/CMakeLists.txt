cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ps5_offline_host)
spiffs_create_partition_image(web "${CMAKE_CURRENT_SOURCE_DIR}/site" FLASH_IN_PROJECT)
