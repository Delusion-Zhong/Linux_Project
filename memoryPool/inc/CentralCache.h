// 中心缓存

#ifndef CENTRAL_CACHE_H
#define CENTRAL_CACHE_H
#include "Conmmon.h"
#include "PageCache.h"
#include <array>
#include <atomic>
#include <csignal>
#include <thread>

class CentralCache
{
public:
    /**
     * @brief 获取 CentralCache 的单例实例
     * @return CentralCache& 返回中心缓存的单例引用
     *
     * 使用单例模式确保整个系统只有一个中心缓存实例
     */
    static CentralCache &getInstance()
    {
        static CentralCache instance;
        return instance;
    }

    /**
     * @brief 从中心缓存获取指定索引的内存范围
     * @param index 内存块大小的索引
     * @param batchNum 批量获取的内存块数量
     * @return T* 获取到的内存指针
     */
    template <typename T, typename N>
    T *fetchRange(N index, N batchNum)
    {
        // 检查索引是否在有效范围内，以及批量获取数量是否为正
        if (index >= FREE_LIST_SIZE || batchNum == 0)
            return nullptr;

        /**
         * @brief 尝试获取锁
         *  使用的是 memory_order_acquire    获取顺序
         *  所有后续的内存读写操作不会被重排到该  内存序操作之前
         *
         * 使用std::atomic_flag::test_and_set()方法尝试获取锁。
         * 如果锁已经被其他线程持有，则返回false，表示获取失败。
         * 如果锁未被持有，则将锁设置为锁定状态，并返回true，表示获取成功。
         * 使用自旋锁，避免线程上下文切换，提高并发性能。
         * ! 获取锁成功后继续执行
         */
        while (locks_[index].test_and_set(std::memory_order_acquire))
        {
            // 让出当前线程的执行时间片，避免在锁竞争中无谓地消耗CPU资源
            std::this_thread::yield();
        }

        T *result = nullptr;
        try
        {
            /**
             * @brief 获取锁成功后，从中央缓存中获取内存块
             *  使用的是 memory_order_relaxed 宽松顺序
             *  不保证内存操作的顺序，只保证操作的原子性
             */
            result = centralFreeList_[index].load(std::memory_order_relaxed);
            if (!result)
            {
                // 找到具体的内存块 （下标+1）*8
                N size = (index + 1) * ALIGNMENT;
                // 如果中央缓存中没有内存块，则从页缓存获取内存块
                result = fetchFromPageCache<T, N>(size);
                if (!result)
                {
                    locks_[index].clear(std::memory_order_release);
                    return nullptr;
                }
                char *start = static_cast<char *>(result);
                // 计算可以分配的内存块数量
                N totalBlocks = (SPAN_PAGES * PAGE_SIZE) / size;
                // 计算实际可以分配的内存块数量
                N allocBlocks = std::min(batchNum, totalBlocks);
                if (allocBlocks > 1)
                {
                    // 如果可以分配的内存块数量大于1，则将内存块连接成链表
                    for (N i = 1; i < allocBlocks; ++i)
                    {
                        void *current = start + (i - 1) * size;
                        void *next = start + i * size;
                        *reinterpret_cast<void **>(current) = next;
                    }
                    *reinterpret_cast<void **>(start + (allocBlocks - 1) * size) = nullptr;
                }
                // 构建保留在CentralCache的链表
                if (totalBlocks > allocBlocks)
                {
                    void *remainStart = start + allocBlocks * size;
                    for (size_t i = allocBlocks + 1; i < totalBlocks; ++i)
                    {
                        void *current = start + (i - 1) * size;
                        void *next = start + i * size;
                        *reinterpret_cast<void **>(current) = next;
                    }
                    *reinterpret_cast<void **>(start + (totalBlocks - 1) * size) = nullptr;

                    centralFreeList_[index].store(remainStart, std::memory_order_release);
                }
            }
            else  // 如果中心缓存有index对应大小的内存块
            {
                // 从现有链表中获取指定数量的块
                void *current = result;
                void *prev = nullptr;
                size_t count = 0;

                while (current && count < batchNum)
                {
                    prev = current;
                    current = *reinterpret_cast<void **>(current);
                    count++;
                }

                if (prev)  // 当前centralFreeList_[index]链表上的内存块大于batchNum时需要用到
                {
                    *reinterpret_cast<void **>(prev) = nullptr;
                }

                centralFreeList_[index].store(current, std::memory_order_release);
            }
        }
        catch (...)
        {
            locks_[index].clear(std::memory_order_release);
            throw;
        }

        // 释放锁
        locks_[index].clear(std::memory_order_release);
        return result;
    }


    /**
     * @brief 将内存范围返回给中心缓存
     * @param ptr 要返回的内存指针
     * @param size 内存块大小
     * @param bytes 实际使用的字节数
     */
    template <typename T, typename N>
    void returnRange(T *ptr, N size, N index)
    {

        // 当索引大于等于FREE_LIST_SIZE时，说明内存过大应直接向系统归还
        if (!ptr || index >= FREE_LIST_SIZE)
            return;

        while (locks_[index].test_and_set(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        try
        {
            // 找到要归还的链表的最后一个节点
            T *end = ptr;
            N count = 1;
            while (*reinterpret_cast<T **>(end) != nullptr && count < size)
            {
                end = *reinterpret_cast<T **>(end);
                count++;
            }

            // 将归还的链表连接到中心缓存的链表头部
            T *current = centralFreeList_[index].load(std::memory_order_relaxed);
            *reinterpret_cast<T **>(end) = current;  // 将原链表头接到归还链表的尾部
            centralFreeList_[index].store(
                ptr,
                std::memory_order_release);  // 将归还的链表头设为新的链表头
        }
        catch (...)
        {
            locks_[index].clear(std::memory_order_release);
            throw;
        }

        locks_[index].clear(std::memory_order_release);
    }

private:
    /**
     * @brief 私有构造函数，初始化中心缓存
     *
     * 初始化自由链表和自旋锁数组
     */
    CentralCache()
    {
        for (auto &ptr : centralFreeList_)
        {
            /**
             * @brief 存储一个空指针到自由链表中
             * @param ptr 要存储的指针
             * @param order 宽松顺序
             */
            ptr.store(nullptr, std::memory_order_relaxed);
        }
        for (auto &lock : locks_)
        {
            /**
             * @brief 清除自旋锁

             */
            lock.clear();
        }
    }

    /**
     * @brief 从中心缓存获取指定大小的内存
     * @param size 请求的内存大小
     * @return void* 获取到的内存指针
     */
    template <typename T, typename N>
    T *fetchFromPageCache(N size)
    {
        N Size_Index = (size + PAGE_SIZE - 1) / PAGE_SIZE;
        if (Size_Index <= SPAN_PAGES * PAGE_SIZE)
        {  // 小于等于32KB的请求，使用固定8页
            return PageCache::GetInstance().allocateSpan<T, N>(SPAN_PAGES);
        }
        else
        {
            // 大于32KB的请求，按实际需求分配
            return PageCache::GetInstance().allocateSpan<T, N>(Size_Index);
        }
    }


private:
    /**
     * @brief 中心缓存自由链表数组
     *
     * 这个数组存储了各种不同大小内存块的空闲链表头指针，数组大小由FREE_LIST_SIZE常量决定，
     * 每个索引位置对应一种特定大小的内存块。
     *
     * 数组中每个元素是std::atomic<void*>类型，使用原子操作确保在多线程环境下安全访问和修改，
     * 避免了数据竞争，提高了并发性能。
     *
     * 在内存布局上，这是一个连续的数组，每个元素占用一个指针大小（通常是4字节或8字节，取决于系统位数）。
     * 数组的每个元素初始化为nullptr，表示该大小类别的空闲链表最初为空。
     *
     * 在实际工作中，当线程请求或释放内存时，会按照内存大小找到对应的索引位置，然后
     * 操作该位置的链表头指针，实现内存的分配和回收。
     */
    std::array<std::atomic<void *>, FREE_LIST_SIZE> centralFreeList_;

    /**
     * @brief 自旋锁数组
     *
     * 这个数组与centralFreeList_一一对应，为每个自由链表提供独立的锁保护机制，
     * 实现了细粒度的锁控制，提高了并发访问效率。
     *
     * 使用std::atomic_flag实现自旋锁而非互斥锁，自旋锁在竞争不激烈的情况下可以减少
     * 线程上下文切换的开销，特别适合短时间持有的锁场景。
     *
     * 在内存布局上，每个atomic_flag通常占用1个字节，整个数组是连续分配的内存区域。
     * 每个锁初始化为未锁定状态（通过clear()方法）。
     *
     * 在多线程访问时，当一个线程需要修改特定大小类别的链表时，会先尝试获取对应的锁，
     * 确保同一时刻只有一个线程能修改该链表，避免数据不一致问题。
     * 不同大小类别的内存操作可以并行进行，提高了系统吞吐量。
     */
    std::array<std::atomic_flag, FREE_LIST_SIZE> locks_;
};

#endif