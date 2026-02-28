// LRUK.hpp
 
#ifndef __LRUK_HPP__
#define __LRUK_HPP__
 
#include <iostream>
#include <memory>
#include <unordered_map>
#include "LRU.hpp"
 
namespace myCache
{
    /**
     * @brief LRU-K 缓存类
     * 核心思想：数据访问满 K 次才进入热点缓存，能够有效过滤偶发性的访问请求。
     * 继承自 LRUCache，作为其“主缓存（热点队列）”。
     */
    template <class Key, class Value>
    class LRUKCache : public LRUCache<Key, Value>
    {
    public:
        /**
         * @brief 构造函数
         * @param capacity 主缓存（热点队列）容量
         * @param historyCapacity 历史队列（访问不足K次）容量
         * @param k 晋升阈值：访问达到 k 次的数据会被移入主缓存
         */
        LRUKCache(int capacity, int historyCapacity, int k)
            : LRUCache<Key, Value>(capacity), 
              _k(k),
              _historyList(std::make_unique<LRUCache<Key, size_t>>(historyCapacity)) 
        {}
 
        /**
         * @brief 获取数据
         * 逻辑：先看热点队列，再更新历史计数。
         */
        Value get(Key key)
        {
            // 1. 尝试从主缓存（热点队列）中读取
            Value value{};
            bool inMainCache = LRUCache<Key, Value>::get(key, value);
 
            // 2. 获取并增加该 Key 的访问历史计数
            // 如果历史队列中不存在，get 会返回 0（取决于 LRU 内部实现）
            size_t historyCount = _historyList->get(key);
            historyCount++;
            _historyList->put(key, historyCount);
 
            // 3. 如果主缓存命中，直接返回（因为已在热点队列，只需更新其在 LRU 中的位置）
            if(inMainCache)
            {
                return value;
            }
 
            // 4. 若主缓存未命中，检查历史访问次数是否达到阈值 K
            if(historyCount >= _k)
            {
                auto it = _historyValueMap.find(key);
                if(it != _historyValueMap.end())
                {
                    // 计数达标，将数据从“历史暂存区”晋升到“主缓存热点队列”
                    Value storedValue = it->second;  
                    
                    // 清理历史记录（不再是“新人”了）
                    _historyList->remove(key);
                    _historyValueMap.erase(it);
 
                    // 正式进入主缓存
                    LRUCache<Key, Value>::put(key, storedValue);
 
                    return storedValue;
                }
            }
            
            // 数据未达标或不存在，返回默认值
            return value;
        }
 
        /**
         * @brief 存入数据
         */
        void put(Key key, Value value)
        {
            // 1. 如果数据已在主缓存中，直接更新其值和热度
            Value exitingValue{};
            if(LRUCache<Key, Value>::get(key, exitingValue))
            {
                LRUCache<Key, Value>::put(key, value);
                return;
            }
            
            // 2. 如果数据不在主缓存，更新其在历史队列的访问次数
            size_t historyCount = _historyList->get(key);
            historyCount++;
            _historyList->put(key, historyCount);
 
            // 3. 将具体数值暂存在映射表中，防止数据在没进主缓存前丢失
            _historyValueMap[key] = value;
 
            // 4. 判定是否达到晋升条件（访问满 K 次）
            if(historyCount >= _k)
            {
                // 移除历史信息
                _historyList->remove(key);
                _historyValueMap.erase(key);
                
                // 将数据“转正”移入主缓存
                LRUCache<Key, Value>::put(key, value);
            }
        }
 
    public:
        int _k;                                              // 进入热点缓存的访问次数门槛
        std::unique_ptr<LRUCache<Key, size_t>> _historyList; // 历史访问频率队列（内部也是个 LRU）
        std::unordered_map<Key, Value> _historyValueMap;     // 存储尚未晋升的数据实际内容
    };
}
 
#endif