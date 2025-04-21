cmake_minimum_required(VERSION 3.10)

# 定义项目名称
project(MemoryPool VERSION 1.0)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加头文件路径
include_directories(${PROJECT_SOURCE_DIR}/inc)

# 源文件
file(GLOB SOURCES 
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
    EXCLUDE "${PROJECT_SOURCE_DIR}/src/PageCache.cpp"
)

# 测试文件
set(TEST_SOURCE "${PROJECT_SOURCE_DIR}/test/pressure_test.cpp")

# 创建静态库
add_library(mempool_static STATIC ${SOURCES})

# 创建可执行文件
add_executable(pressure_test ${TEST_SOURCE})

# 将静态库链接到可执行文件
target_link_libraries(pressure_test mempool_static)

# 可选：安装规则
install(TARGETS pressure_test DESTINATION bin)
install(TARGETS mempool_static DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/inc/ DESTINATION include/mempool)

# 启用测试
enable_testing()
add_test(NAME MemoryPoolTest COMMAND pressure_test)

# 如果有外部库依赖，可以添加
# find_package(SomeLibrary REQUIRED)
# target_link_libraries(${PROJECT_NAME} PRIVATE SomeLibrary) 