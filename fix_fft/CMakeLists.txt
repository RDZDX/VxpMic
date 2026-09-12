cmake_minimum_required (VERSION 3.8)

set(PROJECT_NAME "fix_fft")

add_library(${PROJECT_NAME} STATIC 
	fix_fft.c fix_fft.h
)

target_include_directories(${PROJECT_NAME} PUBLIC ".")