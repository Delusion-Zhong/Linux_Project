// 页缓存

#pragma once
#include "Conmmon.h"
#include <cstddef>
#include <map>
#include <mutex>
#include <sys/mman.h>
/**
 * @class PageCache
 * @brief 页面缓存管理类，管理系统级内存分配
 *
 * PageCache 使用 Span 结构来管理内存块，每个 Span 包含一定数量的连续页，
 * 通过 std::map 映射页数到对应的空闲 Span 链表，实现高效的内存分配和回收。
 */
class PageCache
{
public:
    /**
     * @brief 获取 PageCache 的单例实例
     * @return PageCache& 返回 PageCache 的引用
     *
     * 使用 static 成员变量实现单例模式
     */
    static PageCache &GetInstance()
    {
        static PageCache instance;
        return instance;
    }
    /**
     * @brief 分配指定页数的内存
     * @param numPages 请求的页数
     * @return void* 分配的内存块起始地址，失败返回nullptr
     *
     * 分配流程：
     * 1. 在空闲Span映射表中查找合适大小的Span
     * 2. 如果找到，可能需要分割Span
     * 3. 如果没找到，向系统申请新内存
     */
    template <typename T, typename N>
    T *allocateSpan(N numPages)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 返回一个大于或者等于 numPages 的第一个Span
        auto it = FreeSpans_.lower_bound(numPages);
        // 如果it 不等于 FreeSpans_ 的end
        if (it != FreeSpans_.end())
        {
            // 获取到 it对应的值
            Span *span = it->second;
            // 如果span 的next 不为空
            if (span->next)
            {
                // 获取到key  就是 span 中的next
                FreeSpans_[it->first] = span->next;
            }
            else
            {
                // 如果当前Span链表中没有后续节点
                // 直接从FreeSpans_中删除这个大小的映射
                FreeSpans_.erase(it);
            }
            if (span->sizePages > numPages)
            {
                // 创建新的Span管理剩余内存
                Span *newSpan = new Span;
                // 计算新Span的起始地址：原地址 + 已分配页数 * 页大小
                newSpan->PageAddr = static_cast<char *>(span->PageAddr) + numPages * PAGE_SIZE;
                // 新Span的页数为原Span页数减去已分配页数
                newSpan->sizePages = span->sizePages - numPages;
                newSpan->next = nullptr;

                // 将newspan 中的 sizePages 当做主键存在 map中
                auto &list = FreeSpans_[newSpan->sizePages];
                /*
                    这里就是链表的头插法
                    list就是整个链表的头部指针，将新的内存指向lsit  newSpan 为新的 节点
                    然后lsit=newSpan  就是让list 这个头指针 指向newSpan 起始地址 更新头指针
                */
                newSpan->next = list;
                list = newSpan;
                // 将传入的字节数设置到 span 中
                span->sizePages = numPages;
            }
            // 在SpanMap_中记录分配的Span，便于后续释放
            SpanMap_[span->PageAddr] = span;
            return span->PageAddr;
        }

        // 如果没有找到合适大小的Span，向系统申请新内存
        T *memory = systemaAlloc<N>(numPages);
        if (!memory)
        {
            return nullptr;
        }
        // 创建新的Span管理新分配的内存
        Span *span = new Span;
        span->PageAddr = memory;
        span->sizePages = numPages;
        span->next = nullptr;

        // 记录span信息用于回收
        SpanMap_[memory] = span;
        return memory;
    }


    /**
     * @brief 释放内存回页面缓存
     * @param ptr 要释放的内存指针
     * @param numPages 释放的页数
     *
     * 释放流程：
     * 1. 查找对应的Span
     * 2. 尝试合并相邻的空闲Span
     * 3. 将合并后的Span放回空闲链表
     */
    template <typename T, typename N>
    void deallocateSpan(T *ptr, N numPages)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 查找和ptr 对应的span
        auto it = SpanMap_.find(ptr);
        if (it == SpanMap_.end())
        {
            return;
        }
        // 获取到ptr 对应的span
        Span *span = it->second;

        // 将指针转换为字节数+将页数转换为实际的字节数当前span的下一个span
        auto *nextAddr = static_cast<char *>(ptr) + numPages * PAGE_SIZE;
        // 查找nextAddr 对应的span
        auto nextIt = SpanMap_.find(nextAddr);
        // 如果nextAddr 对应的span 存在
        if (nextIt != SpanMap_.end())
        {
            // 如果找到了相邻的Span，就获取到nextSpan
            Span *nextSpan = nextIt->second;
            bool found = false;
            // 获取相邻Span大小对应的空闲链表,NextLis 是链表头部指针引用
            auto &NextList = FreeSpans_[nextSpan->sizePages];
            // 如果nextSpan 是链表的头部指针
            if (NextList == nextSpan)
            {
                // 如果nextSpan 是链表的头部指针，就将nextSpan的next 赋值给NextList
                NextList = nextSpan->next;
                found = true;
            }
            else if (NextList)
            {
                // 如果nextSpan 不是链表的头部指针，就遍历链表，找到nextSpan
                Span *prev = NextList;
                while (prev->next)
                {
                    // 如果prev的next 是nextSpan
                    if (prev->next == nextSpan)
                    {
                        // 将nextSpan从空闲链表中移除
                        prev->next = nextSpan->next;
                        found = true;
                        break;
                    }
                    prev = prev->next;
                }
            }
            // 只有在找到nextSpan的情况下才进行合并
            if (found)
            {
                // 合并span
                span->sizePages += nextSpan->sizePages;
                SpanMap_.erase(nextAddr);
                delete nextSpan;
            }
        }

        // 将合并后的span通过头插法插入空闲列表
        auto &list = FreeSpans_[span->sizePages];
        span->next = list;
        list = span;
    }

private:
    PageCache() = default;

    /**
     * @brief 向系统申请内存
     * @param numPages 请求的页数
     * @return void* 分配的内存指针，失败返回nullptr
     *
     * 使用mmap系统调用直接向操作系统申请内存
     */
    template <typename T, typename N>
    T *systemaAlloc(N numPages)
    {
        // 计算需要分配的总字节数
        N size = numPages * PAGE_SIZE;

        // 使用mmap分配内存
        T *ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED)
            return nullptr;

        // 初始化内存为0
        memset(ptr, 0, size);
        return ptr;
    }


private:
    /**
     * @struct Span
     * @brief 内存页面管理结构
     *
     * Span 结构表示一段连续的内存页，用于管理大块内存
     */
    struct Span
    {
        void *PageAddr;    ///< 页面的起始地址
        size_t sizePages;  ///< 页面的数量
        Span *next;        ///< 指向下一个Span的指针，形成链表
    };

    /**
     * 空闲Span映射表，以页数为键，对应页数的Span链表为值
     * 使用std::map便于查找最接近请求页数的Span
     * size_t 是主键类型，Span* 是值类型
     */
    std::map<size_t, Span *> FreeSpans_;

    /**
     * 已分配Span映射表，以内存地址为键，Span指针为值
     * 用于在内存释放时快速找到对应的Span
     */
    std::map<void *, Span *> SpanMap_;

    /**
     * 互斥锁，保证多线程环境下的线程安全
     */
    std::mutex mutex_;
};