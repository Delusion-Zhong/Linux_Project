// #include "../inc/CentralCache.h"
// #include "../inc/PageCache.h"
// #include <atomic>
// #include <sys/types.h>
// #include <thread>

// template <typename T, typename N>
// T *CentralCache::fetchRange(N index, N batchNum)
// {
//     // 检查索引是否在有效范围内，以及批量获取数量是否为正
//     if (index >= FREE_LIST_SIZE || batchNum == 0)
//         return nullptr;

//     /**
//      * @brief 尝试获取锁
//      *  使用的是 memory_order_acquire    获取顺序
//      *  所有后续的内存读写操作不会被重排到该  内存序操作之前
//      *
//      * 使用std::atomic_flag::test_and_set()方法尝试获取锁。
//      * 如果锁已经被其他线程持有，则返回false，表示获取失败。
//      * 如果锁未被持有，则将锁设置为锁定状态，并返回true，表示获取成功。
//      * 使用自旋锁，避免线程上下文切换，提高并发性能。
//      * ! 获取锁成功后继续执行
//      */
//     while (locks_[index].test_and_set(std::memory_order_acquire))
//     {
//         // 让出当前线程的执行时间片，避免在锁竞争中无谓地消耗CPU资源
//         std::this_thread::yield();
//     }

//     T *result = nullptr;
//     try
//     {
//         /**
//          * @brief 获取锁成功后，从中央缓存中获取内存块
//          *  使用的是 memory_order_relaxed 宽松顺序
//          *  不保证内存操作的顺序，只保证操作的原子性
//          */
//         result = centralFreeList_[index].load(std::memory_order_relaxed);
//         if (!result)
//         {
//             // 找到具体的内存块 （下标+1）*8
//             N size = (index + 1) * ALIGNMENT;
//             // 如果中央缓存中没有内存块，则从页缓存获取内存块
//             result = fetchFromPageCache<T, N>(size);
//             if (!result)
//             {
//                 locks_[index].clear(std::memory_order_release);
//                 return nullptr;
//             }
//             char *start = static_cast<char *>(result);
//             // 计算可以分配的内存块数量
//             N totalBlocks = (SPAN_PAGES * PAGE_SIZE) / size;
//             // 计算实际可以分配的内存块数量
//             N allocBlocks = std::min(batchNum, totalBlocks);
//             if (allocBlocks > 1)
//             {
//                 // 如果可以分配的内存块数量大于1，则将内存块连接成链表
//                 for (N i = 1; i < allocBlocks; ++i)
//                 {
//                     void *current = start + (i - 1) * size;
//                     void *next = start + i * size;
//                     *reinterpret_cast<void **>(current) = next;
//                 }
//                 *reinterpret_cast<void **>(start + (allocBlocks - 1) * size) = nullptr;
//             }
//             // 构建保留在CentralCache的链表
//             if (totalBlocks > allocBlocks)
//             {
//                 void *remainStart = start + allocBlocks * size;
//                 for (size_t i = allocBlocks + 1; i < totalBlocks; ++i)
//                 {
//                     void *current = start + (i - 1) * size;
//                     void *next = start + i * size;
//                     *reinterpret_cast<void **>(current) = next;
//                 }
//                 *reinterpret_cast<void **>(start + (totalBlocks - 1) * size) = nullptr;

//                 centralFreeList_[index].store(remainStart, std::memory_order_release);
//             }
//         }
//         else  // 如果中心缓存有index对应大小的内存块
//         {
//             // 从现有链表中获取指定数量的块
//             void *current = result;
//             void *prev = nullptr;
//             size_t count = 0;

//             while (current && count < batchNum)
//             {
//                 prev = current;
//                 current = *reinterpret_cast<void **>(current);
//                 count++;
//             }

//             if (prev)  // 当前centralFreeList_[index]链表上的内存块大于batchNum时需要用到
//             {
//                 *reinterpret_cast<void **>(prev) = nullptr;
//             }

//             centralFreeList_[index].store(current, std::memory_order_release);
//         }
//     }
//     catch (...)
//     {
//         locks_[index].clear(std::memory_order_release);
//         throw;
//     }

//     // 释放锁
//     locks_[index].clear(std::memory_order_release);
//     return result;
// }


// template <typename T, typename N>
// void CentralCache::returnRange(T *ptr, N size, N index)
// {
    
//     // 当索引大于等于FREE_LIST_SIZE时，说明内存过大应直接向系统归还
//     if (!ptr || index >= FREE_LIST_SIZE)
//         return;

//     while (locks_[index].test_and_set(std::memory_order_acquire))
//     {
//         std::this_thread::yield();
//     }

//     try
//     {
//         // 找到要归还的链表的最后一个节点
//         T *end = ptr;
//         N count = 1;
//         while (*reinterpret_cast<T **>(end) != nullptr && count < size)
//         {
//             end = *reinterpret_cast<T **>(end);
//             count++;
//         }

//         // 将归还的链表连接到中心缓存的链表头部
//         T *current = centralFreeList_[index].load(std::memory_order_relaxed);
//         *reinterpret_cast<T **>(end) = current;  // 将原链表头接到归还链表的尾部
//         centralFreeList_[index].store(ptr,
//                                       std::memory_order_release);  // 将归还的链表头设为新的链表头
//     }
//     catch (...)
//     {
//         locks_[index].clear(std::memory_order_release);
//         throw;
//     }

//     locks_[index].clear(std::memory_order_release);
// }

// template <typename T, typename N>
// T *CentralCache::fetchFromPageCache(N size)
// {
//     N Size_Index = (size + PAGE_SIZE - 1) / PAGE_SIZE;
//     if (Size_Index <= SPAN_PAGES * PAGE_SIZE)
//     {  // 小于等于32KB的请求，使用固定8页
//         return PageCache::GetInstance().allocateSpan<T, N>(SPAN_PAGES);
//     }
//     else
//     {
//         // 大于32KB的请求，按实际需求分配
//         return PageCache::GetInstance().allocateSpan<T, N>(Size_Index);
//     }
// }

// // 显式实例化模板
// template void CentralCache::returnRange<void, unsigned long>(void*, unsigned long, unsigned long);
// template void* CentralCache::fetchRange<void, unsigned long>(unsigned long, unsigned long);
// template void* CentralCache::fetchFromPageCache<void, unsigned long>(unsigned long);
