cmake_minimum_required (VERSION 3.8)

set(PROJECT_NAME "mic")

add_library(${PROJECT_NAME} STATIC 
	mic.c mic.h
)

target_include_directories(${PROJECT_NAME} PUBLIC ".")

target_link_libraries(${PROJECT_NAME} mreapi)