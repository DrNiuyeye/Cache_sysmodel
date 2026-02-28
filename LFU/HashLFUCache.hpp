// HashLFUCache.hpp
 
#ifndef __HASHLFUCACHE_HPP__
#define __HASHLFUCACHE_HPP__
 
#include <iostream>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <climits>
#include <vector>
#include <thread>
#include <cmath>
#include "LFUCache.hpp"
 
namespace myCache
{
    /**
     * @brief HashLFUCache 类模板
     * 核心思想：通过哈希分片降低锁粒度。
     * 适用场景：高并发环境。传统的单锁 LFU 在多核 CPU 下会因为锁竞争成为性能瓶颈，
     * 分片方案可以将冲突概率降低到原来的 1/sliceNum。
     */
    template <class Key, class Value>
    class HashLFUCache
    {
    private:
        /**
         * @brief 哈希定位函数
         * 使用标准库提供的 hash 对象将 Key 映射为无符号整数
         */
        size_t Hash(Key key)
        {
            std::hash<Key> hashFunc;
            return hashFunc(key);
        }
 
    public:
        /**
         * @brief 构造函数
         * @param capacity 整个缓存的总容量
         * @param sliceNum 分片数量。若传入 0 或负数，则自动设为硬件支持的并发线程数。
         * @param maxAverageNum LFU 内部老化机制的阈值，用于防止“频率老龄化”
         */
        HashLFUCache(size_t capacity, int sliceNum, int maxAverageNum = 10)
            : _sliceNum(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency()),
              _capacity(capacity)    
        {
            // 均匀分配容量：计算每个分片应有的容量上限（向上取整）
            size_t sliceSize = std::ceil(_capacity / static_cast<double>(_sliceNum));
            
            // 初始化分片容器，装载独占的子 LFU 缓存
            for(int i = 0; i < _sliceNum; i++)
            {
                _LFUSliceCaches.emplace_back(std::make_shared<LFUCache<Key, Value>>(sliceSize, maxAverageNum));
            }
        }
 
        /**
         * @brief 写入数据
         * 根据 Key 的哈希值找到对应的分片，然后将写操作委托给该分片
         */
        void put(Key key, Value value)
        {
            size_t sliceIndex = Hash(key) % _sliceNum;
            _LFUSliceCaches[sliceIndex]->put(key, value);
        }
 
        /**
         * @brief 获取数据（引用传参）
         * @param key 键
         * @param value 输出参数，用于接收找到的值
         * @return bool 是否命中缓存
         */
        bool get(Key key, Value& value)
        {
            size_t sliceIndex = Hash(key) % _sliceNum;
            return _LFUSliceCaches[sliceIndex]->get(key, value);
        }
 
        /**
         * @brief 获取数据（直接返回）
         * 若未命中则返回 Value 类型的默认构造值
         */
        Value get(Key key)
        {
            Value value{}; // 使用值初始化确保安全
            get(key, value);
            return value;
        }
 
        /**
         * @brief 清空所有分片缓存
         * 遍历每一个子 LFU 缓存并执行其清理逻辑
         */
        void purge()
        {
            for(auto& cache : _LFUSliceCaches)
            {
                cache->purge();
            }
        }
        
    private:
        size_t _capacity; // 缓存总额度
        int _sliceNum;    // 分片数量
        // 存储切片 LFU 缓存的容器，使用智能指针管理生命周期
        std::vector<std::shared_ptr<LFUCache<Key, Value>>> _LFUSliceCaches; 
    };
}
 
#endif