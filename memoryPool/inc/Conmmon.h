/**
 * @file Conmmon.h
 * @brief 内存池的全局常量定义和配置参数
 *
 * 定义了内存池各层使用的常量、宏和配置参数，
 * 包括内存对齐、大小限制、页面大小等。
 */

#ifndef COMMON_H
#define COMMON_H

#include <algorithm>
#include <cstddef>
/**
 * 内存对齐的最小值，设置为8b
 * 1. 保证所有分配的内存都是8字节对齐的
 * 2. 满足大多数数据类型的对齐要求
 * 3. 有利于提高内存访问性能
 */
static constexpr size_t ALIGNMENT = 8;

/**
 * 内存池管理的最大内存块大小：256KB
 * 1. 超过这个大小的内存请求将直接使用系统malloc
 * 2. 256KB是一个比较合理的阈值，能覆盖大多数小对象分配
 * 3. 避免内存池管理过大的内存块，影响性能
 */
static constexpr size_t MAX_BYTES = 256 * 1024;

/**
 * 自由链表的大小（数组大小）
 * 1. 计算方法：最大内存大小 / 对齐大小  （256KB / 8B = 32768B）
 * 2. 例如：256KB / 8B = 32768，表示有32768个不同大小的内存块类别
 * 3. 每个位置i对应的内存块大小为：(i + 1) * ALIGNMENT
 * 4. 形成内存块大小序列：8B, 16B, 24B, ..., 256KB
 */
static constexpr size_t FREE_LIST_SIZE = MAX_BYTES / ALIGNMENT;


/**
 * 内存池中Span（内存块组）的默认页面数量
 * 1. 一个Span是由连续的多个页面组成的内存管理单元
 * 2. 设置为8页表示每个Span默认包含8个连续的内存页面
 * 3. 这个值影响内存分配的粒度和效率：
 *    - 较小的值会减少内存碎片，但增加管理开销
 *    - 较大的值会减少管理开销，但可能增加内存碎片
 * 4. 8页是经验值，在内存利用率和管理效率间取得平衡
 */
static constexpr size_t SPAN_PAGES = 8;  // 一个span 包含8页


/**
 * 页面大小定义为4KB（4096字节）
 * 与操作系统的内存页大小一致，提高内存管理效率
 * 在Linux系统中，标准页面大小通常为4KB
 */
static constexpr size_t PAGE_SIZE = 4096;  // 4k 页大小


// 内存块头部信息
struct BlockHeader
{
    size_t size;        // 内存块大小
    bool inUse;         // 使用标志
    BlockHeader *next;  // 指向下一个内存块
};

// 大小类管理
class SizeClass
{
public:
    /**
     * @brief 将内存大小向上对齐到 ALIGNMENT 的倍数
     * @param bytes 需要对齐的内存大小
     * @return size_t 对齐后的内存大小
     *
     * 使用位运算实现高效的对齐操作：
     * 1. 计算方式：(bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1)
     * 2. 例如：ALIGNMENT = 8
     *    - 输入 10 -> 输出 16
     *    - 输入 15 -> 输出 16
     *    - 输入 16 -> 输出 16
     */
    static size_t roundUp(size_t bytes)
    {
        /*
        *假设 bytes = 10
         1. 10 + 7 = 17
       二进制：0001 0001

        2. 17 & ~7
        0001 0001
        1111 1000
        ---------
        0001 0000 = 16

            结果：10 被向上取整到 16
         */
        //  这里  ~(ALIGNMENT - 1) 转换为二进制进行取反，然后进行比较
        return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
    }

    /**
     * @brief 计算内存大小对应的自由链表索引
     * @param bytes 内存大小
     * @return size_t 自由链表数组的索引
     *
     * 计算规则：
     * 1. 确保输入大小至少为 ALIGNMENT
     * 2. 将大小除以 ALIGNMENT 并减1得到索引
     * 3. 例如：ALIGNMENT = 8
     *    - 输入 8 -> 输出 0
     *    - 输入 16 -> 输出 1
     *    - 输入 24 -> 输出 2
     */
    static size_t getIndex(size_t bytes)
    {
        // 确保bytes至少为ALIGNMENT
        bytes = std::max(bytes, ALIGNMENT);
        // 向上取整后-1
        return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
    }
};

#endif  // COMMON_H
