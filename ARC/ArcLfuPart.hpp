// ArcLfuPart.hpp

#ifndef __ARC_LFU_PART_HPP__
#define __ARC_LFU_PART_HPP__

#include <iostream>
#include <unordered_map>
#include <map>
#include <memory>
#include <vector>
#include <list>
#include <mutex>
#include "../Common/ArcCacheNode.hpp"

namespace myCache
{
    /**
     * @brief ArcLfuPart 类模板
     * 负责管理 ARC 算法中具有“高频访问”特征的数据。
     * 内部采用 频率->节点链表 的映射结构，实现 O(1) 的频率平滑升级。
     */
    template <class Key, class Value>
    class ArcLfuPart
    {
    public:
        typedef ArcNode<Key, Value> NodeType;
        typedef std::shared_ptr<NodeType> NodePtr;
        typedef std::unordered_map<Key, NodePtr> NodeMap;
        // 使用 std::map 维护频率到节点的映射，map 默认按频率升序排列，方便找最小频率
        typedef std::map<size_t, std::list<NodePtr>> FreqMap;

    private:
        /**
         * @brief 更新已存在节点的值并提升其频率
         */
        bool updateExistingNode(NodePtr node, const Value& value)
        {
            node->setValue(value);
            updateNodeFrequency(node);
            return true;
        }

        /**
         * @brief 添加新节点到 LFU 部分
         * 注意：在完整的 ARC 逻辑中，通常只有从 LRU 晋升过来的节点会进入这里
         */
        bool addNewNode(const Key& key, const Value& value)
        {
            if(_mainCache.size() >= _capacity)
            {
                // 容量满，根据 LFU 策略驱逐频率最低且最旧的节点
                evictLeastFrequent();
            }
            NodePtr newNode = std::make_shared<NodeType>(key, value);
            _mainCache[key] = newNode;

            // 初始频率设为 1（或根据晋升时的实际频率设置）
            if(_freqMap.find(1) == _freqMap.end())
            {
                _freqMap[1] = std::list<NodePtr>();
            }
            _freqMap[1].push_front(newNode);
            _minFreq = 1;

            return true;
        }

        /**
         * @brief 频率提升逻辑
         * 将节点从旧频率链表移动到新频率（freq + 1）链表
         */
        void updateNodeFrequency(NodePtr node)
        {
            size_t oldFreq = node->getAccessCount();
            node->incrementAccessCount(); // 访问计数 +1
            size_t newFreq = node->getAccessCount();

            // 1. 从原频率对应的 list 中移除
            auto &oldList = _freqMap[oldFreq];
            oldList.remove(node); // 注意：std::list::remove 是 O(n)，若追求极致性能可存储 list 迭代器

            // 2. 如果旧频率列表空了，清理它并更新全局最小频率指针
            if(oldList.empty())
            {
                _freqMap.erase(oldFreq);
                if(oldFreq == _minFreq)
                {
                    _minFreq = newFreq;
                }
            }

            // 3. 挂载到新频率列表的末尾（保持同频节点间的 LRU 顺序）
            if(_freqMap.find(newFreq) == _freqMap.end())
            {
                _freqMap[newFreq] = std::list<NodePtr>();
            }
            _freqMap[newFreq].push_back(node);
        }

        /**
         * @brief 淘汰逻辑：驱逐频率最低的节点并将其存入 Ghost (B2) 列表
         */
        void evictLeastFrequent()
        {
            if(_freqMap.empty())
                return;

            auto &minFreqList = _freqMap[_minFreq];
            if(minFreqList.empty())
                return;

            // 取出最小频率链表中最旧的节点（队首）
            NodePtr leastNode = minFreqList.front();
            minFreqList.pop_front();

            if(minFreqList.empty())
            {
                _freqMap.erase(_minFreq);
                if(!_freqMap.empty())
                {
                    // 由于 std::map 是有序的，其 begin() 即为当前的最小频率
                    _minFreq = _freqMap.begin()->first;
                }
            }

            // --- 幽灵缓存处理 ---
            if(_ghostCache.size() >= _ghostCapacity)
            {
                removeOldestGhost();
            }
            addToGhost(leastNode);

            // 从物理主缓存映射中移除数据
            _mainCache.erase(leastNode->getKey());
        }

        /**
         * @brief 从 Ghost 链表中物理断开节点
         */
        void removeFromGhost(NodePtr node)
        {
            if(!node->_prev.expired() && node->_next)
            {
                auto prev = node->_prev.lock();
                prev->_next = node->_next;
                node->_next->_prev = prev;
                node->_next = nullptr;
            }
        }

        /**
         * @brief 将节点移入 Ghost 列表（B2），只保留元数据痕迹
         */
        void addToGhost(NodePtr node)
        {
            // 插入 Ghost 链表的尾部
            node->_next = _ghostTail;
            node->_prev = _ghostTail->_prev;
            if(!_ghostTail->_prev.expired())
            {
                _ghostTail->_prev.lock()->_next = node;
            }
            _ghostTail->_prev = node;
            _ghostCache[node->getKey()] = node;
        }

        /**
         * @brief 彻底清理 Ghost 列表中最久远的记录
         */
        void removeOldestGhost()
        {
            NodePtr oldestGhost = _ghostHead->_next;
            if(oldestGhost != _ghostTail)
            {
                removeFromGhost(oldestGhost);
                _ghostCache.erase(oldestGhost->getKey());
            }
        }

    public:
        /**
         * @brief 构造函数
         * @param capacity LFU 部分的初始容量（ARC 运行时会动态调整此值）
         * @param transformThreshold 暂时未在内部显式使用，通常由外部控制
         */
        explicit ArcLfuPart(size_t capacity, size_t transformThreshold)
            : _capacity(capacity),
              _ghostCapacity(capacity),
              _transformThreshold(transformThreshold),
              _minFreq(0),
              _ghostHead(std::make_shared<NodeType>()),
              _ghostTail(std::make_shared<NodeType>())
        {
            _ghostHead->_next = _ghostTail;
            _ghostTail->_prev = _ghostHead;
        }

        /**
         * @brief 写入/更新接口
         */
        bool put(Key key, Value value)
        {
            if(_capacity == 0)
                return false;
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _mainCache.find(key);
            if(it != _mainCache.end())
            {
                return updateExistingNode(it->second, value);
            }
            return addNewNode(key, value);
        }

        /**
         * @brief 读取并提升节点频率
         */
        bool get(Key key, Value& value)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _mainCache.find(key);
            if(it != _mainCache.end())
            {
                updateNodeFrequency(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }

        /**
         * @brief 检查节点是否存在于热缓存
         */
        bool contain(Key key)
        {
            return _mainCache.find(key) != _mainCache.end();
        }

        /**
         * @brief 幽灵快查：在 B2 列表中检查是否存在访问记录
         * 如果命中，说明此 Key 曾是高频数据，这会触发 ARC 增大 LFU 部分的权重
         */
        bool checkGhost(Key key)
        {
            auto it = _ghostCache.find(key);
            if(it != _ghostCache.end())
            {
                removeFromGhost(it->second);
                _ghostCache.erase(it);
                return true;
            }
            return false;
        }

        // --- 动态容量管理（供 ARC 主控逻辑调用） ---

        void increaseCapacity() { _capacity++; }

        bool decreaseCapacity()
        {
            if(_capacity <= 0)
            {
                _capacity = 0;
                return false;
            }
            // 缩小容量时，若当前存储已满，需先驱逐一个节点
            if(_mainCache.size() == _capacity)
            {
                evictLeastFrequent();   
            }
            --_capacity;
            return true;
        }
        
    private:
        size_t _capacity;           // LFU 主缓存（T2）容量
        size_t _ghostCapacity;      // 幽灵记录（B2）最大容量
        size_t _transformThreshold; // 频率转换阈值
        size_t _minFreq;            // 全局最小频率标识
        std::mutex _mutex;

        NodeMap _mainCache;         // Key -> 节点指针 (T2)
        NodeMap _ghostCache;        // Key -> 节点指针 (B2，只存元数据)
        FreqMap _freqMap;           // 频率分层索引
        
        NodePtr _ghostHead;         // Ghost 链表哨兵头
        NodePtr _ghostTail;         // Ghost 链表哨兵尾
    };
}

#endif