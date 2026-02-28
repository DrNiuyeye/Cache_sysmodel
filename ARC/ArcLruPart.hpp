// ArcLruPart.hpp

#ifndef __ARCLRU_PART_HPP__
#define __ARCLRU_PART_HPP__

#include <iostream>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "../Common/ArcCacheNode.hpp"

namespace myCache
{
    /**
     * @brief ArcLruPart 负责管理 ARC 算法中的 LRU 逻辑部分（通常对应 T1 和 B1 列表）
     */
    template<class Key, class Value>
    class ArcLruPart
    {
    public:
        typedef ArcNode<Key, Value> NodeType;
        typedef std::shared_ptr<NodeType> NodePtr;
        typedef std::unordered_map<Key, NodePtr> NodeMap;

    private:
        /**
         * @brief 更新已存在的节点：修改值并移动到 LRU 链表头部
         */
        bool updateExistingNode(NodePtr node, const Value& value) 
        {
            node->setValue(value);
            moveToFront(node);
            return true;
        }

        /**
         * @brief 添加全新的节点到 LRU 主缓存
         * 如果空间不足，会触发淘汰机制进入 Ghost 链表
         */
        bool addNewNode(const Key& key, const Value& value)
        {
            if(_mainCache.size() >= _capacity)
            {
                // 空间满，驱逐末尾节点（Least Recently Used）
                evictLeastRecent();
            }
            NodePtr newNode = std::make_shared<NodeType>(key, value);
            _mainCache[key] = newNode;
            addToFront(newNode);
            return true;
        }

        /**
         * @brief 更新节点访问状态
         * @return bool 返回该节点是否达到了晋升为 LFU 节点的阈值
         */
        bool updateNodeAccess(NodePtr node)
        {
            moveToFront(node);
            node->incrementAccessCount();
            // 如果访问次数足够多，ARC 会将其从 T1(LRU) 移动到 T2(LFU)
            return node->getAccessCount() >= _transformThreshold;
        }

        /**
         * @brief 将节点从当前链表位置移动到头部
         */
        void moveToFront(NodePtr node)
        {
            removeFromMain(node);
            addToFront(node);
        }
        
        /**
         * @brief 基础操作：将节点挂载到双向链表的首位（哨兵头节点之后）
         */
        void addToFront(NodePtr node)
        {
            node->_next = _mainHead->_next;
            node->_prev = _mainHead;
            _mainHead->_next->_prev = node;
            _mainHead->_next = node;
        }

        /**
         * @brief 核心淘汰逻辑：将节点从主缓存移除，并放入“幽灵”链表（Ghost Cache）
         * 这是 ARC 算法捕获“历史痕迹”的关键步骤
         */
        void evictLeastRecent()
        {
            NodePtr leastRecent = _mainTail->_prev.lock();
            if(!leastRecent || leastRecent == _mainHead)
                return;

            // 1. 从主物理链表移除
            removeFromMain(leastRecent);
            // 2. 从主哈希映射中移除（数据不再真正存储）
            _mainCache.erase(leastRecent->getKey());   

            // 3. 进入 Ghost 链表（只保留 Key 的访问痕迹）
            if(_ghostCache.size() >= _ghostCapacity)
            {
                removeOldestGhost();
            }
            addToGhost(leastRecent);
        }

        /**
         * @brief 物理移除：断开节点在主链表中的前后连接
         */
        void removeFromMain(NodePtr node)
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
         * @brief 物理移除：断开节点在淘汰（Ghost）链表中的前后连接
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
         * @brief 存入幽灵链表：重置访问计数并存入 Ghost 映射
         */
        void addToGhost(NodePtr node)
        {
            node->_accessCount = 1;
            // 挂载到 Ghost 链表头部
            node->_next = _ghostHead->_next;
            node->_prev = _ghostHead;
            _ghostHead->_next->_prev = node;
            _ghostHead->_next = node;

            _ghostCache[node->getKey()] = node;
        }

        /**
         * @brief 彻底销毁：从幽灵链表中删除最老的痕迹
         */
        void removeOldestGhost()
        {
            NodePtr leastRecent = _ghostTail->_prev.lock();
            if(!leastRecent || leastRecent == _ghostHead)
                return;

            removeFromGhost(leastRecent);
            _ghostCache.erase(leastRecent->getKey());
        }

    public:
        /**
         * @brief 构造函数：初始化主链表和幽灵链表的哨兵节点
         */
        explicit ArcLruPart(size_t capacity, size_t transfromThreshold)
            : _capacity(capacity),
              _ghostCapacity(capacity),
              _transformThreshold(transfromThreshold),
              _mainHead(std::make_shared<NodeType>()),
              _mainTail(std::make_shared<NodeType>()),
              _ghostHead(std::make_shared<NodeType>()),
              _ghostTail(std::make_shared<NodeType>())
        {
            _mainHead->_next = _mainTail;
            _mainTail->_prev = _mainHead;
            _ghostHead->_next = _ghostTail;
            _ghostTail->_prev = _ghostHead;
        }
        
        /**
         * @brief 外部写入接口
         * @return bool 是否成功操作（在 ARC 整体逻辑中可能触发晋升判断）
         */
        bool put(Key key, Value value)
        {
            if(_capacity == 0) return false;
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _mainCache.find(key);
            if(it != _mainCache.end())
            {
                return updateExistingNode(it->second, value);
            }
            return addNewNode(key, value);
        }

        /**
         * @brief 外部读取接口
         * @param shouldTransform 输出参数，告知外部调用者此节点是否由于访问频繁需要移动到 LFU 部分
         */
        bool get(Key key, Value& value, bool& shouldTransform)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _mainCache.find(key);
            if(it != _mainCache.end())
            {
                shouldTransform = updateNodeAccess(it->second);
                value = it->second->getValue();
                return true;
            }
            return false;
        }

        /**
         * @brief 幽灵快查：检查 Key 是否在淘汰痕迹中
         * 如果命中，说明此 Key 之前被访问过但被踢出了，这会触发 ARC 的权重调整（增加 LRU 链表的配额）
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

        // --- 动态容量调整接口（ARC 算法的核心能力） ---

        void increaseCapacity() { ++_capacity; }

        bool decreaseCapacity()
        {
            if(_capacity <= 0) { _capacity = 0; return false; }
            if(_mainCache.size() == _capacity)
            {
                evictLeastRecent();
            }
            --_capacity;
            return true;
        }

    private:
        size_t _capacity;           // 当前 LRU 部分允许存储的数据量
        size_t _ghostCapacity;      // 记录淘汰痕迹的最大数量
        size_t _transformThreshold; // 晋升为 LFU 节点的访问门槛
        std::mutex _mutex;

        NodeMap _mainCache;         // 热数据哈希映射
        NodeMap _ghostCache;        // 淘汰痕迹哈希映射

        NodePtr _mainHead;          // LRU 双向链表头
        NodePtr _mainTail;          // LRU 双向链表尾
        NodePtr _ghostHead;         // Ghost 双向链表头
        NodePtr _ghostTail;         // Ghost 双向链表尾
    };
}

#endif