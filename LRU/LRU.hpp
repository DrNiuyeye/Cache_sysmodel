#ifndef __LRU_HPP__
#define __LRU_HPP__

#include <memory>
#include <unordered_map>   
#include <mutex>
#include "../Common/CachePolicy.hpp"

namespace myCache
{
    // 前向声明，方便 LRUNode 声明友元
    template<class Key, class Value> class LRUCache;
    /**
     * @brief LRU双向链表节点
     */
    template<class Key, class Value>
    class LRUNode
    {
        typedef LRUNode<Key,Value> Node;
        friend class LRUCache<Key, Value>; // 允许 LRUCache 访问私有成员
    private:
        Key _key;
        Value _value;
        size_t _accessCount;        // 统计该节点的访问次数
        std::weak_ptr<Node> _prev;  // 指向前驱节点，使用 weak_ptr 防止与 next 形成循环引用导致内存泄漏
        std::shared_ptr<Node> _next;// 指向后继节点
    public:
        LRUNode(Key key, Value value): _key(key), _value(value), _accessCount(1){}
        Key getKey() const { return _key; }
        Value getValue() const { return _value; }
        void setValue(const Value& value) { _value = value; }
        size_t getAccessCount() const { return _accessCount; }
        void incrementAccessCount() { _accessCount++; }
    };

    /**
     * @brief 基于双向链表和哈希表的 LRU 缓存实现
     * 逻辑：最近访问的放在尾部(tail)，最久未访问的放在头部(head)
     */
    template<class Key, class Value>
    class LRUCache : public CachePolicy<Key, Value>
    {
        typedef LRUNode<Key, Value> Node;
        typedef std::shared_ptr<Node> NodePtr;
        typedef std::unordered_map<Key, NodePtr> NodeMap;

    private:
        /**
         * @brief 更新已存在的节点：修改值并移动到链表末尾
         */
        void updateExistringNode(NodePtr node, const Value& value)
        {
            node->setValue(value);
            moveToMostRecent(node);
        }

        /**
         * @brief 添加新节点：处理容量检查并插入到末尾
         */
        void addNewNode(const Key& key, const Value& value)
        {
            if(_nodeMap.size() >= _capacity)
            {
                evictLeastRecent(); // 缓存满，驱逐最久未使用的节点
            }
            NodePtr newNode = std::make_shared<Node>(key, value);
            _nodeMap[key] = newNode;
            insertNode(newNode);
        }

        /**
         * @brief 将节点标记为最近使用
         */
        void moveToMostRecent(NodePtr node)
        {
            removeNode(node); // 先从当前位置断开
            insertNode(node); // 重新插入到 tail 之前
        }

        /**
         * @brief 从双向链表中逻辑删除节点（不处理 map）
         */
        void removeNode(NodePtr node)
        {
            // 确保节点确实在链表中（有前驱和后继）
            if(!node->_prev.expired() && node->_next)
            {
                auto prev = node->_prev.lock();
                prev->_next = node->_next;
                node->_next->_prev = prev;
                // 清理当前节点的指针指向，防止残留
                node->_next = nullptr;
                node->_prev.reset();
            }
        }

        /**
         * @brief 驱逐最久未使用的节点（靠近 head 的有效节点）
         */
        void evictLeastRecent()
        {
            NodePtr leastRecent = _head->_next; // head 之后第一个是真正的数据节点
            removeNode(leastRecent);
            _nodeMap.erase(leastRecent->getKey()); // 处理map
        }
        
        /**
         * @brief 将节点插入到链表尾部（_tail 之前）
         */
        void insertNode(NodePtr node)
        {
            node->_next = _tail;
            node->_prev = _tail->_prev;
            _tail->_prev.lock()->_next = node; // 让原本在末尾的那个节点指向新节点
            _tail->_prev = node;
        }

    public:
        /**
         * @brief 初始化 LRU 缓存
         * @param capacity 缓存容量上限
         */
        LRUCache(int capacity)
            : _capacity(capacity)
        {
            // 创建虚拟头尾节点（Sentinel Nodes），简化边界条件判断
            _head = std::make_shared<Node>(Key(), Value());
            _tail = std::make_shared<Node>(Key(), Value());
            _head->_next = _tail;
            _tail->_prev = _head;
        }

        ~LRUCache() override = default;
        
        void put(Key key, Value value) override
        {
            if(_capacity <= 0) return;
            
            std::lock_guard<std::mutex> lock(_mutex); // 线程安全保证
            auto it = _nodeMap.find(key);
            if(it != _nodeMap.end())
            {
                updateExistringNode(it->second, value);
                return;
            }
            addNewNode(key, value);
        }

        bool get(Key key, Value& value) override
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _nodeMap.find(key);
            if(it != _nodeMap.end())
            {
                moveToMostRecent(it->second); // 访问即更新位置
                value = it->second->getValue();
                return true;
            }
            return false;
        }

        Value get(Key key) override
        {
            Value value;
            if(get(key, value))
                return value;
            return value; // 未找到则返回默认值
        }

        /**
         * @brief 手动删除指定 Key 的缓存项
         */
        void remove(Key key)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _nodeMap.find(key);
            if(it != _nodeMap.end())
            {
                removeNode(it->second);
                _nodeMap.erase(it);
            }
        }

    private:
        int _capacity;           // 缓存最大容量
        NodeMap _nodeMap;        // 哈希表：Key -> 节点指针，实现 O(1) 查找
        std::mutex _mutex;       // 互斥锁，支持多线程安全
        NodePtr _head;           // 虚拟头节点：指向“最久未使用”的方向
        NodePtr _tail;           // 虚拟尾节点：指向“最近使用”的方向
    };
}

#endif
