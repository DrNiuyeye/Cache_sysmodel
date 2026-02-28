// LFU-Cache.hpp
 
#ifndef __LFUCACHE_HPP__
#define __LFUCACHE_HPP__
 
#include <iostream>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <climits>
#include "../Common/CachePolicy.hpp"
 
namespace myCache
{
    // 前向声明，方便 FreqList 引用
    template <class Key, class Value> class LFUCache;
 
    /**
     * @brief 频率链表类
     * 管理所有具有【相同访问频率】的节点。内部是一个双向链表。
     */
    template <class Key, class Value>
    class FreqList
    {
        friend class LFUCache<Key, Value>;
 
    private:
        // 节点结构：包含数据本身、访问频率以及双向指针
        struct Node
        {
            int freq;           // 当前节点的访问频次
            Key key;
            Value value;
            std::weak_ptr<Node> pre;   // 前驱指针（弱引用防止循环计数）
            std::shared_ptr<Node> next;// 后继指针
 
            Node() : freq(1), next(nullptr) {}
            Node(Key key, Value value) : freq(1), key(key), value(value), next(nullptr) {}
        };
 
        typedef std::shared_ptr<Node> NodePtr;
        int _freq;     // 本链表对应的统一频率
        NodePtr _head; // 哨兵头节点（不存数据）
        NodePtr _tail; // 哨兵尾节点（不存数据）
 
    public:
        explicit FreqList(int freq)
            : _freq(freq),
              _head(std::make_shared<Node>()),
              _tail(std::make_shared<Node>())
        {
            _head->next = _tail;
            _tail->pre = _head;
        }
 
        // 检查当前频率下是否还有节点
        bool isEmpty() const { return _head->next == _tail; }
 
        // 将节点添加到链表末尾（表示该频率下最新访问的）
        void addNode(NodePtr node)
        {
            if (!node || !_head || !_tail) return;
            node->pre = _tail->pre;
            node->next = _tail;
            _tail->pre.lock()->next = node;
            _tail->pre = node;
        }
 
        // 从当前链表中移除指定节点
        void removeNode(NodePtr node)
        {
            if (!node || !_head || !_tail) return;
            if (node->pre.expired() || node->next == nullptr) return;
            auto pre = node->pre.lock();
            pre->next = node->next;
            node->next->pre = pre;
            node->next = nullptr; 
        }
 
        // 获取该频率下“最老”的节点（用于淘汰）
        NodePtr getFirstNode() const { return _head->next; }
    };
 
    /**
     * @brief LFU 缓存核心类
     */
    template <class Key, class Value>
    class LFUCache : public CachePolicy<Key, Value>
    {
    public:
        typedef typename FreqList<Key, Value>::Node Node;
        typedef std::shared_ptr<Node> NodePtr;
        typedef std::unordered_map<Key, NodePtr> NodeMap;
 
    private:
        /**
         * @brief 内部写入逻辑
         * 处理新成员入场或满员踢人
         */
        void putInternal(Key key, Value value)
        {
            if(_nodeMap.size() == _capacity)
            {
                // 缓存满：踢掉频率最低且最久没用的那个
                kickOut();
            }
            NodePtr node = std::make_shared<Node>(key, value);
            _nodeMap[key] = node;
            addToFreqList(node); // 加入频率为 1 的链表
            addFreqNum();        // 更新全局统计信息
            _minFreq = 1;        // 新成员进来，最小频率肯定重置为 1
        }
 
        /**
         * @brief 内部读取逻辑
         * 负责数据的频率升级（从当前链表移动到 freq+1 的链表）
         */
        void getInternal(NodePtr node, Value &value)
        {
            value = node->value;
            removeFromFreqList(node); // 从原频率链表移除
            node->freq++;             // 频率加 1
            addToFreqList(node);      // 加入新频率链表
            
            // 如果原本最小频率的链表空了，且刚升级的节点就是之前的最小频率节点，更新全局最小频率
            if(_minFreq == node->freq - 1 && _freqToFreqList[node->freq - 1]->isEmpty())
                _minFreq++;
                
            addFreqNum(); // 更新平均值统计
        }
 
        // 淘汰逻辑：寻找最小频率链表中的第一个节点删除
        void kickOut()
        {
            NodePtr node = _freqToFreqList[_minFreq]->getFirstNode();
            int decreaseNum = node->freq;
            _nodeMap.erase(node->key);
            removeFromFreqList(node);
            decreaseFreqNum(decreaseNum); // 更新总频次
        }
 
        void removeFromFreqList(NodePtr node)
        {
            if(!node) return;
            _freqToFreqList[node->freq]->removeNode(node);
        }
 
        void addToFreqList(NodePtr node)
        {
            if(!node) return;
            int freq = node->freq;
            // 如果这个频率的链表还没创建，先创建一个
            if(_freqToFreqList.find(freq) == _freqToFreqList.end())
            {
                _freqToFreqList[freq] = std::make_shared<FreqList<Key, Value>>(freq);
            }
            _freqToFreqList[freq]->addNode(node);
        }
 
        /**
         * @brief 统计逻辑：增加总访问频次，并动态计算平均访问频次
         * 用于解决频率“老龄化”问题
         */
        void addFreqNum()
        {
            _curTotalNum++;
            _curAverageNum = _nodeMap.empty() ? 0 : _curTotalNum / _nodeMap.size();
            // 如果平均访问次数太高，触发平衡机制，防止频率无限增长
            if(_curAverageNum > _maxAverageNum)
                handleOverMaxAverageNum();
        }    
 
        void decreaseFreqNum(int num)
        {
            _curTotalNum -= num;
            _curAverageNum = _nodeMap.empty() ? 0 : _curTotalNum / _nodeMap.size();
        }
 
        /**
         * @brief 平衡机制：将所有节点的频率减半
         * 防止某些数据早期访问极多，后期变冷门却因高频率无法被淘汰（缓存污染）
         */
        void handleOverMaxAverageNum()
        {
            if(_nodeMap.empty()) return;
            int cnt = 0;
            for(auto it = _nodeMap.begin(); it != _nodeMap.end(); )
            {
                if(!it->second) { it = _nodeMap.erase(it); continue; }
                
                ++cnt;
                NodePtr node = it->second;
                removeFromFreqList(node);
                node->freq -= _maxAverageNum / 2; // 频率整体削减
                if(node->freq < 1) node->freq = 1;
                addToFreqList(node);
                ++it;
            }
            _curTotalNum -= (_maxAverageNum / 2) * cnt;
            _curAverageNum = _curTotalNum / cnt;
            updateMinFreq(); // 重新寻找当前全局最小频率
        }
 
        void updateMinFreq()
        {
            _minFreq = INT_MAX;
            for(const auto &pair : _freqToFreqList)
            {
                if(pair.second && !pair.second->isEmpty())
                    _minFreq = std::min(_minFreq, pair.first);
            }
            if(_minFreq == INT_MAX) _minFreq = 1;
        }
 
    public:
        LFUCache(int capacity, int maxAverageNum = 10)
            : _capacity(capacity), _minFreq(INT_MAX),
              _maxAverageNum(maxAverageNum), _curAverageNum(0), _curTotalNum(0)
        {}
 
        ~LFUCache() override = default;
 
        void put(Key key, Value value) override
        {
            if(_capacity == 0) return;
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _nodeMap.find(key);
            if(it != _nodeMap.end()) // 已存在，更新值并升频
            {
                it->second->value = value;
                getInternal(it->second, value);
                return;
            }
            putInternal(key, value); // 不存在，新插
        }
 
        bool get(Key key, Value &value) override
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _nodeMap.find(key);
            if(it != _nodeMap.end())
            {
                getInternal(it->second, value);
                return true;
            }
            return false;
        }
 
        Value get(Key key) override
        {
            Value value{};
            get(key, value);
            return value;
        }
 
        void purge()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _nodeMap.clear();
            _freqToFreqList.clear(); // 智能指针会自动回收内存
        }
 
    private:
        int _capacity;          // 缓存总容量
        int _minFreq;           // 全局最小访问频率（淘汰时的搜索起点）
        int _maxAverageNum;     // 触发频率缩减的阈值
        int _curAverageNum;     // 当前平均频率
        int _curTotalNum;       // 历史访问总次数（权重总和）
        std::mutex _mutex;      // 线程安全锁
        NodeMap _nodeMap;       // 快速定位：Key -> 节点
        // 频率映射：频率 -> 该频率下的双向链表
        std::unordered_map<int, std::shared_ptr<FreqList<Key, Value>>> _freqToFreqList;
    };
}
 
#endif
