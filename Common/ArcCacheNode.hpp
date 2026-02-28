// ArcCacheNode.hpp

#ifndef __ARC_CACHE_NODE_HPP__
#define __ARC_CACHE_NODE_HPP__

#include <memory>

namespace myCache
{
    // 前向声明，用于在 ArcNode 中定义友元类
    template<class K, class V> class ArcLruPart;
    template<class K, class V> class ArcLfuPart;

    /**
     * @brief ArcNode 缓存节点类
     * 它是双向链表的基本单元，存储了实际的数据、访问计数以及前驱/后继指针。
     */
    template<class Key, class Value>
    class ArcNode
    {
        // 声明友元类，允许 ARC 的 LRU 部分和 LFU 部分直接访问私有成员，提高操作效率
        template<class K, class V>
        friend class ArcLruPart;
        template<class K, class V>
        friend class ArcLfuPart;

    private:
        Key _key;                 // 缓存的键
        Value _value;             // 缓存的值
        size_t _accessCount;      // 该节点被访问的次数（用于 LFU 逻辑判断）

        /**
         * 智能指针管理双向链表：
         * _prev 使用 weak_ptr：防止循环引用（Circular Reference）。
         * 如果 _prev 和 _next 都用 shared_ptr，两个节点互相指向会导致引用计数永远无法归零，内存永远无法释放。
         */
        std::weak_ptr<ArcNode> _prev;   // 指向前驱节点的弱引用
        std::shared_ptr<ArcNode> _next; // 指向后继节点的强引用

    public:
        /**
         * @brief 默认构造函数
         */
        ArcNode()
            : _accessCount(1),
              _next(nullptr)
        {}

        /**
         * @brief 数据节点构造函数
         * @param key 键
         * @param value 值
         */
        ArcNode(Key key, Value value)
            : _key(key),
              _value(value),
              _accessCount(1), // 节点创建时即为第一次访问
              _next(nullptr)
        {}

        Key getKey() const
        {
            return _key;
        }
        
        Value getValue() const
        {
            return _value;
        }

        size_t getAccessCount() const
        {
            return _accessCount;
        }

        void setValue(const Value& value)
        {
            _value = value;
        }

        /**
         * @brief 增加访问计数
         * 当节点被命中时调用，用于 ARC 算法判断是否将节点从 LRU 部分移动到 LFU 部分
         */
        void incrementAccessCount()
        {
            _accessCount++;
        }
    };
}

#endif
