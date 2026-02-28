// ArcCache.hpp

#ifndef __ARC_CACHE_HPP__
#define __ARC_CACHE_HPP__

#include <iostream>
#include <memory>
#include "ArcLruPart.hpp"
#include "ArcLfuPart.hpp"
#include "../Common/CachePolicy.hpp"

namespace myCache
{
    /**
     * @brief ArcCache 类模板
     * 继承自 CachePolicy 基类，是 ARC 算法的顶层实现。
     * 它组合了 LRU 分量和 LFU 分量，并根据“幽灵命中”动态调整两者的配额。
     */
    template<class Key, class Value>
    class ArcCache : public CachePolicy<Key,Value>
    {
    private:
        /**
         * @brief 检查幽灵缓存并执行自适应调整
         * 这是 ARC 的灵魂所在：
         * 1. 如果命中 LRU 的幽灵缓存：说明最近被踢出的新数据其实很有用，应该增加 LRU 部分的容量。
         * 2. 如果命中 LFU 的幽灵缓存：说明高频数据被错误踢出了，应该增加 LFU 部分的容量。
         * @return bool 是否命中任何幽灵缓存
         */
        bool checkGhostCaches(Key key)
        {
            bool inGhost = false;
            // 情况 A：在 LRU 的幽灵列表中找到（说明该 Key 刚被 LRU 踢出不久又被访问了）
            if(_lruPart->checkGhost(key))
            {
                // 策略：缩小 LFU 空间，挪给 LRU
                if(_lfuPart->decreaseCapacity())
                {
                    _lruPart->increaseCapacity();
                }
                inGhost = true;
            }
            // 情况 B：在 LFU 的幽灵列表中找到（说明该 Key 曾是高频数据，踢出它是个错误）
            else if(_lfuPart->checkGhost(key))
            {
                // 策略：缩小 LRU 空间，挪给 LFU
                if(_lruPart->decreaseCapacity())
                {
                    _lfuPart->increaseCapacity();
                }
                inGhost = true;
            }
            return inGhost;
        }   

    public:
        /**
         * @brief 构造函数
         * @param capacity 缓存总容量
         * @param transformThreshold 晋升门槛（访问多少次后从 LRU 转入 LFU）
         */
        explicit ArcCache(size_t capacity = 10, size_t transformThreshold = 2)
            :_capacity(capacity),
             _transformThreshold(transformThreshold),
             _lruPart(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold)),
             _lfuPart(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
        {}

        ~ArcCache() override = default;

        /**
         * @brief 写入数据
         * 首先检查是否存在幽灵命中以调整容量权重，然后优先放入 LRU 部分。
         */
        void put(Key key, Value value) override
        {
            // 1. 尝试根据历史痕迹调整 LRU/LFU 的配额比例
            checkGhostCaches(key);

            // 2. 默认存入 LRU 部分（作为新晋数据）
            _lruPart->put(key, value);

            // 3. 如果该数据已经在 LFU 部分存在，同步更新 LFU 中的值
            bool inLfu = _lfuPart->contain(key);
            if(inLfu)
            {
                _lfuPart->put(key, value);
            }
        }

        /**
         * @brief 获取数据
         * 遵循：LRU 查找 -> 晋升判断 -> LFU 查找 的顺序。
         */
        bool get(Key key, Value& value) override
        {
            // 每次访问前先通过幽灵列表学习用户偏好
            checkGhostCaches(key);
            
            bool shouldTransform = false;
            // 1. 先在 LRU（新近数据区）查找
            if(_lruPart->get(key, value, shouldTransform))
            {
                // 如果命中且达到了晋升阈值（如访问了 2 次）
                if(shouldTransform)
                {
                    // 将其从 LRU 移动（晋升）到 LFU 长期关注区
                    _lfuPart->put(key, value);
                }
                return true;
            }

            // 2. 若 LRU 未命中，去 LFU（高频数据区）查找
            return _lfuPart->get(key, value);   
        }

        /**
         * @brief 获取数据（直接返回值版本）
         */
        Value get(Key key) override
        {
            Value value{};
            get(key, value);
            return value;
        }

    private:
        size_t _capacity;           // 总容量上限
        size_t _transformThreshold; // 节点从 LRU 提升到 LFU 的阈值
        
        // ARC 的两个子引擎
        std::unique_ptr<ArcLruPart<Key, Value>> _lruPart;
        std::unique_ptr<ArcLfuPart<Key, Value>> _lfuPart;
    };
}

#endif
