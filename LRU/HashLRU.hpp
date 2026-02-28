// HashLRUCache.hpp
 
#ifndef __HASH_LRU_HPP__
#define __HASH_LRU_HPP__
 
#include "LRU.hpp"
#include <vector>
#include <cmath>
#include <thread>
#include <cstring>
 
namespace myCache
{
    /**
     * @brief HashLRUCache 模板类
     * 核心思想：将一个大 LRU 拆分为多个小 LRU。
     * 作用：降低锁的粒度，允许多个线程同时访问不同的分片，从而提升高并发下的吞吐量。
     */
    template<class Key, class Value>
    class HashLRUCache
    {
    private:
        /**
         * @brief 哈希定位函数
         * 根据 Key 计算其对应的哈希值，决定该数据存放在哪一个分片
         */
        size_t Hash(Key key)
        {
            std::hash<Key> hashFunc;
            return hashFunc(key);
        }

    public:
        /**
         * @brief 构造函数
         * @param capacity 总缓存容量
         * @param sliceNum 分片数量（建议设置为 CPU 核心数的 1-2 倍）
         */
        HashLRUCache(size_t capacity, int sliceNum)
            : _capacity(capacity),
              // 如果未指定分片数，默认设置为当前系统的硬件并发核心数
              _sliceNum(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
        {
            // 计算每个分片应分配的容量（向上取整，确保总容量不低于设定值）
            size_t sliceSize = std::ceil(capacity / static_cast<double>(_sliceNum));
            
            // 初始化分片容器，并为每个分片创建一个独立的 LRUCache
            for(int i = 0; i < _sliceNum; i++)
            {
                _LRUSliceCaches.emplace_back(std::make_unique<LRUCache<Key, Value>>(sliceSize));
            }
        }
 
        /**
         * @brief 存入数据
         * 先计算哈希值找到对应的分片，然后在该分片内部进行 put 操作（内部带锁）
         */
        void put(Key key, Value value)
        {
            size_t sliceIndex = Hash(key) % _sliceNum;
            _LRUSliceCaches[sliceIndex]->put(key, value);
        }
 
        /**
         * @brief 获取数据（引用传参方式）
         * @return 是否命中缓存
         */
        bool get(Key key, Value& value)
        {
            size_t sliceIndex = Hash(key) % _sliceNum;
            return _LRUSliceCaches[sliceIndex]->get(key, value);
        }
        
        /**
         * @brief 获取数据（直接返回方式）
         * 若未命中，返回一个内存清零的默认对象
         */
        Value get(Key key)
        {
            Value value;
            // 初始清零处理，防止返回包含随机垃圾数据的对象
            memset(&value, 0, sizeof(Value));
            get(key, value);
            return value;
        }
 
    private:
        size_t _capacity; // 总容量
        int _sliceNum;    // 分片（切片）数量
        // 使用智能指针存储每个分片的 LRU 实例，防止内存泄漏并支持动态初始化
        std::vector<std::unique_ptr<LRUCache<Key, Value>>> _LRUSliceCaches; 
    };
}
 
#endif