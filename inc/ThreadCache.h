// 线程级缓存

#ifndef THREAD_CACHE_H
#define THREAD_CACHE_H
#include "CentralCache.h"
#include "Conmmon.h"
#include <array>
#include <cstddef>

class ThreadCache
{
public:
    /**
     * @brief 获取当前线程的 ThreadCache 实例
     * @return ThreadCache* 返回线程本地缓存的单例指针
     *
     * 使用 thread_local 确保每个线程都有自己独立的 ThreadCache 实例
     * 避免了线程间的竞争，提高了并发性能
     */
    static ThreadCache *getThreadCache()
    {
        static thread_local ThreadCache instance;
        return &instance;
    }

    /**
     * @brief 分配指定大小的内存
     * @param size 请求的内存大小
     * @return  返回的是一个指针
     *
     * 如果请求的大小超过最大限制(MAX_BYTES)，则直接调用系统malloc
     * 否则从线程本地缓存或中心缓存获取内存
     */
    template <typename T, typename N>
    T *allocate(N size)
    {
        if (size == 0)
            size = ALIGNMENT;

        if (size > MAX_BYTES)
        {
            return static_cast<T *>(malloc(size));
        }
        // 内存对齐，获取 向上取整的下标zh
        N index = SizeClass::getIndex(size);
        // 更新自由链表大小
        _freeListSize[index]--;
        // 如果头节点为空，则需要从中心缓存获取内存
        if (void *ptr = _freeList[index])
        {

            /*
                int a=10;
                int *p=&a;
                int **pp=&p;  解引用后的值就是 10;
                当前的freelist[index] 指向的是一个 T* 类型的指针
                解引用后就是 T 的一个值，进行返回当前内存地址的值

            */
            _freeList[index] = *reinterpret_cast<T **>(ptr);
            return reinterpret_cast<T *>(ptr);
        }
        //! 如果这里不为空 则需要从中心缓存获取内存
        return 0;
    }


    /**
     * @brief 释放指定大小的内存
     * @param ptr 指向要释放的内存的指针
     * @param size 请求的内存大小
     *
     */
    template <typename T, typename N>
    void deallocate(T *ptr, N size)
    {
        // 大于就使用free 直接释放
        if (size > MAX_BYTES)
        {
            free(ptr);
            return;
        }
        // 计算索引下标
        N index = SizeClass::getIndex(size);
        // 指针存放的是指针
        *reinterpret_cast<T **>(ptr) = _freeList[index];
        _freeList[index] = ptr;
        _freeListSize[index]++;

        // 判断是否需要将部分内存回收给中心缓存
        if (shuoReturnThreadCache(index))
        {
            returnThreadCache(ptr, size);
        }
    }


private:
    /**
     * @brief 私有构造函数，防止外部创建实例
     *
     * 初始化线程本地缓存，确保只能通过getThreadCache方法获取实例
     */
    ThreadCache() = default;


    /**
     * @brief 判断是否需要归还内存给中心缓存
     * @param index 自由链表的索引
     * @return bool 如果自由链表大小超过阈值返回true，否则返回false
     */
    template <typename N>
    bool shuoReturnThreadCache(N index)
    {
        const size_t thread = 64;
        return (_freeListSize[index] > thread);
    }


    /**
     * @brief 将多余的内存归还给中心缓存
     * @param ptr 要归还的内存块指针
     * @param size 内存块的大小
     */
    template <typename N>
    void returnThreadCache(void *ptr, N size)
    {
        //  计算内存大小对应的自由链表索引
        N index = SizeClass::getIndex(size);
        // 将内存大小向上对齐到 8 的倍数
        N AllSize = SizeClass::roundUp(size);
        // 获取当前链表的大小
        N batchNum = _freeListSize[index];
        if (batchNum <= 1)
        {
            return;
        }
        // 要保留的节点数量
        N RetainNum = std::max(batchNum / 2, size_t(1));

        // 计算出 需要归还的内存
        size_t FreesizeNum = batchNum - RetainNum;

        char *current = static_cast<char *>(ptr);
        char *splitNode = current;
        for (size_t i = 0; i < RetainNum - 1; ++i)
        {
            /**
             * 重要：链表节点遍历与内存指针操作详解
             *
             * 当前内存布局：
             * +------------------+
             * |下一块指针(8字节)   |  <--- splitNode 指向这里（块的起始位置）
             * +------------------+
             * | 实际数据区域      |  <--- 存储用户数据的区域
             * | ...              |
             * +------------------+
             *
             * 指针操作步骤详解：
             * 1. reinterpret_cast<T **>(splitNode)
             *    - 目的：将 splitNode 从 char* 类型转换为 T** 类型
             *    - 作用：使编译器将这段内存解释为"指针的指针"
             *    - 原理：内存块的开头8字节存储的就是下一个块的地址
             *
             * 2. *reinterpret_cast<T **>(splitNode)
             *    - 目的：解引用转换后的指针，获取下一个节点的地址
             *    - 作用：读取当前内存块开头8字节中存储的地址值
             *    - 结果：得到指向下一个内存块的指针值
             *
             * 3. reinterpret_cast<char *>(...)
             *    - 目的：将获取的指针转换回 char* 类型
             *    - 作用：保持指针类型统一，方便后续操作
             *    - 必要性：内存池中统一使用 char* 处理内存
             *
             * 完整过程示例：
             *    如果 splitNode = 0x1000 (当前块地址)
             *    且在地址0x1000处存储的值为0x2000 (下一块地址)
             *    则经过这行代码后，splitNode 将变为 0x2000
             *
             * 访问用户数据（如需）：
             *    要访问实际数据，需要跳过指针部分：
             *    char* dataStart = splitNode + sizeof(T*);
             *    这样 dataStart 就指向了用户数据的起始位置
             *
             *  遍历到临界点 需要返回给中心内存
             */

            splitNode = reinterpret_cast<char *>(*reinterpret_cast<void **>(splitNode));

            // 检查是否到达链表末尾
            if (splitNode == nullptr)
            {
                // 如果提前遇到空指针，更新实际可以返回的数量
                FreesizeNum = batchNum - (i + 1);
                break;
            }
        }
        if (splitNode != nullptr)
        {
            void *nextNode = *reinterpret_cast<void **>(splitNode);
            // 断开连接
            *reinterpret_cast<void **>(splitNode) = nullptr;
            _freeList[index] = nextNode;
            _freeListSize[index] = RetainNum;
            if (RetainNum > 0 && nextNode != nullptr)
            {
                //! 将剩余的内存返回给 中心内存
                CentralCache::getInstance().returnRange(nextNode, FreesizeNum, index);
            }
        }
    }


    /**
     * @brief 从中心缓存获取内存
     *
     * @param index  内存大小
     * @return void*  返回的是一个指针
     */
    template <typename N>
    void *fetchFromCentralCache(N index)
    {
        size_t size = (index + 1) * ALIGNMENT;
        // 根据对象内存大小计算批量获取的数量
        size_t batchNum = getBatchNum(size);
        // 从中心缓存批量获取内存
        void *start = CentralCache::getInstance().fetchRange(index, batchNum);
        if (!start)
            return nullptr;

        // 更新自由链表大小
        _freeListSize[index] += batchNum;  // 增加对应大小类的自由链表大小

        // 取一个返回，其余放入线程本地自由链表
        void *result = start;
        if (batchNum > 1)
        {
            _freeList[index] = *reinterpret_cast<void **>(start);
        }

        return result;
    }


    /**
     * @brief 计算批量获取内存块的数量
     *
     * @param size  内存块的大小
     * @return size_t  数量
     */
    template <typename N>
    N getBatchNum(N size)
    {
        // 基准：每次批量获取不超过4KB内存
        constexpr N MAX_BATCH_SIZE = 4 * 1024;  // 4KB

        // 根据对象大小设置合理的基准批量数
        N baseNum;
        if (size <= 32)
            baseNum = 64;  // 64 * 32 = 2KB
        else if (size <= 64)
            baseNum = 32;  // 32 * 64 = 2KB
        else if (size <= 128)
            baseNum = 16;  // 16 * 128 = 2KB
        else if (size <= 256)
            baseNum = 8;  // 8 * 256 = 2KB
        else if (size <= 512)
            baseNum = 4;  // 4 * 512 = 2KB
        else if (size <= 1024)
            baseNum = 2;  // 2 * 1024 = 2KB
        else
            baseNum = 1;  // 大于1024的对象每次只从中心缓存取1个

        // 计算最大批量数
        N maxNum = std::max(size_t(1), MAX_BATCH_SIZE / size);

        // 取最小值，但确保至少返回1
        return std::max(sizeof(1), std::min(maxNum, baseNum));
    }


    // 每个线程的自由链表数组
    std::array<void *, FREE_LIST_SIZE> _freeList;

    // 每个线程的自由链表大小统计
    std::array<size_t, FREE_LIST_SIZE> _freeListSize;
};

#endif