cmake_minimum_required(VERSION 3.16)
project(408_code CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ---- 库:所有 4 科模块 (目前为 header-only,无 .cpp) ---------------
# 如未来添加 .cpp,取消下面的 GLOB 注释
# file(GLOB_RECURSE MODULE_SOURCES
#     "data-structures/*.cpp"
#     "operating-systems/*.cpp"
#     "computer-organization/*.cpp"
#     "computer-networks/*.cpp"
#     "common/*.cpp"
# )
# add_library(408_modules STATIC ${MODULE_SOURCES})
# target_include_directories(408_modules PUBLIC ${CMAKE_SOURCE_DIR})

# ---- CLI 入口 (header-only 模块直接 include) ----------------------
add_executable(408-cli 408-cli.cpp)
target_include_directories(408-cli PRIVATE ${CMAKE_SOURCE_DIR})

# ---- 测试可执行 ---------------------------------------------------
enable_testing()
add_subdirectory(tests)
