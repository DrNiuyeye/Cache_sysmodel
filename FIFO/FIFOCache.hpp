// FIFOCache.hpp
 
#include <iostream>
#include <queue>
#include <unordered_set>
#include <vector>
 
class FIFOCache
{
public:
    /**
     * @brief 构造函数
     * @param cap 缓存的最大容量
     */
    FIFOCache(int cap) : capacity(cap)
    {}
 
    /**
     * @brief 访问页面请求
     * @param pageNum 待访问的页面编号
     * @return true 表示发生缺页，false 表示页面已在缓存中（命中）
     */
    bool accessPage(int pageNum)
    {
        // 1. 缓存命中：无需任何操作
        if (pageSet.find(pageNum) != pageSet.end())
        {
            return false;
        }
 
        // 2. 发生缺页：
        // 如果当前缓存已达到最大容量，执行置换策略
        if (pageQueue.size() == capacity)
        {
            // 获取并移除队列首部（即最早进入）的页面
            int oldestPage = pageQueue.front();
            pageQueue.pop();
            
            // 同步从哈希表中删除，释放空间
            pageSet.erase(oldestPage);
        }
 
        // 将新请求的页面放入队列尾部并记录到集合中
        pageQueue.push(pageNum);
        pageSet.insert(pageNum);
 
        return true;
    }
 
    /**
     * @brief 打印当前缓存中保存的所有页面（按进入顺序）
     */
    void displayCache()
    {
        std::cout << "当前缓存状态 (最早 -> 最新): [ ";
 
        // std::queue 不支持迭代器遍历，需通过拷贝一份临时队列来读取内容
        std::queue<int> tempQueue = pageQueue;
        while (!tempQueue.empty())
        {
            std::cout << tempQueue.front() << " ";
            tempQueue.pop();
        }
        std::cout << "]" << std::endl;
    }
 
    // 获取当前已占用的缓存大小
    int getCurrentSize()
    {
        return pageQueue.size();
    }
 
    // 获取设定的缓存总容量
    int getCapacity()
    {
        return capacity;
    }
 
private:
    int capacity;                    // 缓存允许的最大容量
    std::queue<int> pageQueue;       // 核心队列：维护页面进入的时间顺序
    std::unordered_set<int> pageSet; // 辅助哈希表：实现 O(1) 复杂度的存在性查找
};
