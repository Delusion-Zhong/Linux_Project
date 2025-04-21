/**
 * @file pressure_test.cpp
 * @brief 内存池压力测试程序
 *
 * 通过多线程并发分配/释放内存的方式测试内存池的性能和稳定性
 */

#include "../inc/ThreadCache.h"
#include <chrono>
#include <cstring>  // 添加cstring头文件以使用memset
#include <iomanip>  // 添加iomanip头文件以使用setprecision
#include <iostream>
#include <random>
#include <thread>
#include <vector>

// 测试参数
constexpr int THREAD_COUNT = 8;                // 线程数量
constexpr int ALLOC_COUNT = 100000;            // 每个线程的分配次数
constexpr int MAX_ALLOC_SIZE = 4096;           // 最大分配大小
constexpr int MIN_ALLOC_SIZE = 8;              // 最小分配大小
constexpr bool COMPARE_WITH_MALLOC = true;     // 是否与系统malloc对比
constexpr bool TEST_SIZE_DISTRIBUTION = true;  // 是否测试不同内存大小的分配性能

// 记录分配的内存及大小
struct MemoryBlock
{
    void *ptr;
    size_t size;
};

// 内存池分配测试的
void memPoolAllocTest(int threadId)
{
    std::vector<MemoryBlock> blocks;
    blocks.reserve(ALLOC_COUNT);

    // 使用随机数生成器
    std::mt19937 gen(threadId);  // 使用线程ID作为种子
    std::uniform_int_distribution<> sizeDist(MIN_ALLOC_SIZE, MAX_ALLOC_SIZE);
    std::uniform_int_distribution<> freeDist(0, 100);  // 控制释放概率

    // 获取线程缓存
    ThreadCache *threadCache = ThreadCache::getThreadCache();

    // 分配和释放内存
    for (int i = 0; i < ALLOC_COUNT; i++)
    {
        // 随机决定是分配还是释放
        if (blocks.empty() || freeDist(gen) > 30)
        {  // 70%概率分配
            // 分配内存
            size_t size = sizeDist(gen);
            void *ptr = threadCache->allocate<size_t>(size);

            // 写入测试数据验证内存可用
            if (ptr)
            {
                memset(ptr, threadId & 0xFF, size);
                blocks.push_back({ptr, size});
            }
        }
        else
        {
            // 释放内存
            int index = freeDist(gen) % blocks.size();
            threadCache->deallocate(blocks[index].ptr, blocks[index].size);

            // 从数组中移除
            blocks[index] = blocks.back();
            blocks.pop_back();
        }
    }

    // 清理剩余内存
    for (const auto &block : blocks)
    {
        threadCache->deallocate(block.ptr, block.size);
    }
}

// 系统malloc分配测试
void mallocTest(int threadId)
{
    std::vector<MemoryBlock> blocks;
    blocks.reserve(ALLOC_COUNT);

    std::mt19937 gen(threadId);
    std::uniform_int_distribution<> sizeDist(MIN_ALLOC_SIZE, MAX_ALLOC_SIZE);
    std::uniform_int_distribution<> freeDist(0, 100);

    for (int i = 0; i < ALLOC_COUNT; i++)
    {
        if (blocks.empty() || freeDist(gen) > 30)
        {  // 70%概率分配
            size_t size = sizeDist(gen);
            void *ptr = malloc(size);

            if (ptr)
            {
                memset(ptr, threadId & 0xFF, size);
                blocks.push_back({ptr, size});
            }
        }
        else
        {
            int index = freeDist(gen) % blocks.size();
            free(blocks[index].ptr);

            blocks[index] = blocks.back();
            blocks.pop_back();
        }
    }

    for (const auto &block : blocks)
    {
        free(block.ptr);
    }
}

// 运行测试并测量时间
template <typename Func>
double runTest(Func testFunc, const std::string &testName)
{
    std::vector<std::thread> threads;
    auto startTime = std::chrono::high_resolution_clock::now();

    // 创建线程
    for (int i = 0; i < THREAD_COUNT; i++)
    {
        threads.push_back(std::thread(testFunc, i));
    }

    // 等待所有线程完成
    for (auto &t : threads)
    {
        t.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    std::cout << testName << " 完成: " << duration << " ms" << std::endl;
    return duration;
}

// 各种大小的内存分配测试
void testSizeDistribution()
{
    std::cout << "\n测试不同内存大小的分配性能:" << std::endl;

    // 测试不同的内存大小
    std::vector<size_t> testSizes = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};

    for (size_t size : testSizes)
    {
        // 使用内存池
        ThreadCache *threadCache = ThreadCache::getThreadCache();
        auto startPool = std::chrono::high_resolution_clock::now();

        std::vector<void *> poolPtrs;
        poolPtrs.reserve(10000);

        // 分配10000个指定大小的内存块
        for (int i = 0; i < 10000; i++)
        {
            void *ptr = threadCache->allocate<size_t>(size);
            if (ptr)
            {
                memset(ptr, 0xAA, size);  // 测试内存可写
                poolPtrs.push_back(ptr);
            }
        }

        // 释放所有分配的内存
        for (void *ptr : poolPtrs)
        {
            threadCache->deallocate(ptr, size);
        }

        auto endPool = std::chrono::high_resolution_clock::now();
        double poolTime = std::chrono::duration<double, std::milli>(endPool - startPool).count();

        // 使用系统malloc
        auto startMalloc = std::chrono::high_resolution_clock::now();

        std::vector<void *> mallocPtrs;
        mallocPtrs.reserve(10000);

        // 分配10000个指定大小的内存块
        for (int i = 0; i < 10000; i++)
        {
            void *ptr = malloc(size);
            if (ptr)
            {
                memset(ptr, 0xAA, size);  // 测试内存可写
                mallocPtrs.push_back(ptr);
            }
        }

        // 释放所有分配的内存
        for (void *ptr : mallocPtrs)
        {
            free(ptr);
        }

        auto endMalloc = std::chrono::high_resolution_clock::now();
        double mallocTime =
            std::chrono::duration<double, std::milli>(endMalloc - startMalloc).count();

        // 计算性能提升
        double speedup = (mallocTime / poolTime) * 100.0 - 100.0;

        std::cout << "大小 " << std::setw(5) << size << " 字节: ";
        std::cout << "内存池 " << std::setw(8) << std::fixed << std::setprecision(3) << poolTime
                  << " ms, ";
        std::cout << "malloc " << std::setw(8) << std::fixed << std::setprecision(3) << mallocTime
                  << " ms, ";
        std::cout << "提升 " << std::setw(8) << std::fixed << std::setprecision(2) << speedup << "%"
                  << std::endl;
    }
}

int main()
{
    std::cout << "开始内存池压力测试..." << std::endl;
    std::cout << "线程数: " << THREAD_COUNT << ", 每线程分配次数: " << ALLOC_COUNT << std::endl;
    std::cout << "分配大小范围: " << MIN_ALLOC_SIZE << " - " << MAX_ALLOC_SIZE << " 字节"
              << std::endl;
    std::cout << "总操作次数: " << THREAD_COUNT * ALLOC_COUNT << std::endl;

    // 运行内存池测试
    double poolTime = runTest(memPoolAllocTest, "内存池测试");

    // 计算每秒操作次数
    double poolOpsPerSec = (THREAD_COUNT * ALLOC_COUNT) / (poolTime / 1000.0);
    double poolTimePerOp = poolTime / (THREAD_COUNT * ALLOC_COUNT);

    std::cout << "内存池性能:" << std::endl;
    std::cout << "- 每秒操作数: " << std::fixed << std::setprecision(2) << poolOpsPerSec
              << " ops/sec" << std::endl;
    std::cout << "- 平均操作时间: " << std::fixed << std::setprecision(6) << poolTimePerOp
              << " ms/op" << std::endl;

    // 可选：运行系统malloc测试作为对比
    double mallocTime = 0;
    if (COMPARE_WITH_MALLOC)
    {
        std::cout << "\n开始执行malloc对比测试..." << std::endl;
        mallocTime = runTest(mallocTest, "系统malloc测试");

        // 计算每秒操作次数
        double mallocOpsPerSec = (THREAD_COUNT * ALLOC_COUNT) / (mallocTime / 1000.0);
        double mallocTimePerOp = mallocTime / (THREAD_COUNT * ALLOC_COUNT);

        std::cout << "系统malloc性能:" << std::endl;
        std::cout << "- 每秒操作数: " << std::fixed << std::setprecision(2) << mallocOpsPerSec
                  << " ops/sec" << std::endl;
        std::cout << "- 平均操作时间: " << std::fixed << std::setprecision(6) << mallocTimePerOp
                  << " ms/op" << std::endl;

        // 计算性能提升
        double speedup = (mallocTime / poolTime) * 100.0 - 100.0;
        std::cout << "\n性能对比结果:" << std::endl;
        std::cout << "内存池相对于系统malloc ";
        if (speedup > 0)
        {
            std::cout << "提升了 " << std::fixed << std::setprecision(2) << speedup << "%"
                      << std::endl;
        }
        else
        {
            std::cout << "慢了 " << std::fixed << std::setprecision(2) << -speedup << "%"
                      << std::endl;
        }
    }

    // 可选：运行不同内存大小的分配性能测试
    if (TEST_SIZE_DISTRIBUTION)
    {
        testSizeDistribution();
    }

    return 0;
}