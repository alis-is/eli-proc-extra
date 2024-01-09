cmake_minimum_required(VERSION 3.13)
project(eli_proc_extra_kill)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${BUILD_FLAGS} -fPIC -fPIE -ffunction-sections -fdata-sections -g0 -Wl,--as-needed -Wno-sign-compare -Wl,--gc-sections -static -DCURL_STATICLIB")
set(LD_FLAGS "${LD_FLAGS} -Wl,-s -Wl,-Bsymbolic -Wl,--gc-sections")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_CRT_SECURE_NO_WARNINGS")

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions")
endif()

add_executable(kill src/main.c)
