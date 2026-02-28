#include "LRU/LRU.hpp"
#include "LRU/LRUK.hpp"
#include "LRU/HashLRU.hpp"
#include "LFU/HashLFUCache.hpp"
#include "LFU/LFUCache.hpp"
#include "FIFO/FIFOCache.hpp"
#include "ARC/ArcCache.hpp"
#include <random>
#include <array>

/**
 * @brief 结果打印辅助函数
 * 计算并输出各算法的命中率及原始数据
 */
void printResults(const std::string &testName, int capacity, const std::vector<int> &get_operations, const std::vector<int> &hits)
{
    std::cout << "=== " << testName << " ===" << std::endl;
    std::cout << "缓存容量：" << capacity << std::endl;
    for (size_t i = 0; i < hits.size(); ++i) {
        std::string algoName = (i == 0 ? "LRU" : (i == 1 ? "LFU" : "ARC"));
        double rate = (get_operations[i] > 0) ? ((double)hits[i] / get_operations[i]) * 100 : 0;
        std::cout << algoName << " - 命中率：" << rate << "%" 
                  << " (" << hits[i] << "/" << get_operations[i] << ")" << std::endl;
    }
}

/**
 * @brief 场景1：热点数据访问测试
 * 模拟典型的“二八原则”：少量数据被频繁访问，大量数据极少访问。
 * LFU 在此场景表现通常最好，ARC 会迅速调整分区指针 p 向 LFU 倾斜。
 */
void testHotDataAccess()
{
    std::cout << "\n=== 测试场景1：热点数据访问测试 ===" << std::endl;

    const int CAPACITY = 20;       
    const int OPERATIONS = 500000; 
    const int HOT_KEYS = 20;       // 热点刚好填满缓存
    const int COLD_KEYS = 5000;    

    myCache::LRUCache<int, std::string> lru(CAPACITY);
    myCache::LFUCache<int, std::string> lfu(CAPACITY);
    // 注意：ARC内部包含LRU和LFU两部分，这里传入CAPACITY/2可能导致总容量与前两者不完全一致，视实现而定
    myCache::ArcCache<int, std::string> arc(CAPACITY / 2);

    std::random_device rd;
    std::mt19937 gen(rd()); //随机数

    std::array<myCache::CachePolicy<int, std::string> *, 3> caches = {&lru, &lfu, &arc};
    std::vector<int> hits(3, 0);           
    std::vector<int> get_operations(3, 0); 

    for (int i = 0; i < caches.size(); ++i)
    {
        // 预热：先将热点数据存入
        for (int key = 0; key < HOT_KEYS; ++key)
        {
            caches[i]->put(key, "value" + std::to_string(key));
        }

        for (int op = 0; op < OPERATIONS; ++op)
        {
            bool isPut = (gen() % 100 < 30); // 30% 写，70% 读
            int key;

            // 70% 概率命中热点，30% 概率触发冷数据（会导致缓存污染）
            if (gen() % 100 < 70) key = gen() % HOT_KEYS; 
            else key = HOT_KEYS + (gen() % COLD_KEYS); 

            if (isPut) caches[i]->put(key, "val" + std::to_string(op));
            else {
                get_operations[i]++;
                std::string result;
                if (caches[i]->get(key, result)) hits[i]++;
            }
        }
    }
    printResults("热点数据访问测试", CAPACITY, get_operations, hits);
}

/**
 * @brief 场景2：循环扫描测试
 * 模拟大批量数据的顺序遍历（如全表扫描）。
 * LRU 在此场景会发生“缓存污染”，即热点被扫过一遍的数据全部踢出。
 * ARC 通过 Ghost 链表感知到这种模式，会保护 LFU 区不被扫描数据清空。
 */
void testLoopPattern()
{
    std::cout << "\n=== 测试场景2：循环扫描测试 ===" << std::endl;

    const int CAPACITY = 50;       
    const int LOOP_SIZE = 500;     // 扫描范围远大于缓存容量
    const int OPERATIONS = 200000; 

    myCache::LRUCache<int, std::string> lru(CAPACITY);
    myCache::LFUCache<int, std::string> lfu(CAPACITY);
    myCache::ArcCache<int, std::string> arc(CAPACITY / 2);

    std::array<myCache::CachePolicy<int, std::string> *, 3> caches = {&lru, &lfu, &arc};
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);

    std::random_device rd;
    std::mt19937 gen(rd());

    for (int i = 0; i < caches.size(); ++i)
    {
        int current_pos = 0;
        for (int op = 0; op < OPERATIONS; ++op)
        {
            bool isPut = (gen() % 100 < 20);
            int key;

            // 混合负载：60%顺序扫描，30%随机访问，10%噪声
            if (op % 100 < 60) {
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            } 
            else if (op % 100 < 90) key = gen() % LOOP_SIZE;
            else key = LOOP_SIZE + (gen() % LOOP_SIZE);

            if (isPut) caches[i]->put(key, "loop" + std::to_string(op));
            else {
                get_operations[i]++;
                std::string result;
                if (caches[i]->get(key, result)) hits[i]++;
            }
        }
    }
    printResults("循环扫描测试", CAPACITY, get_operations, hits);
}

/**
 * @brief 场景3：工作负载剧烈变化测试
 * 这是 ARC 的主场。测试分为 5 个阶段，访问模式在热点、随机、顺序扫描间切换。
 * ARC 的优势在于它能通过“学习”历史命中痕迹，在几秒钟内完成分区比例的自我优化。
 */
void testWorkloadShift()
{
    std::cout << "\n=== 测试场景3：工作负载剧烈变化测试 ===" << std::endl;

    const int CAPACITY = 30;           
    const int OPERATIONS = 80000;      
    const int PHASE_LENGTH = OPERATIONS / 5; 

    myCache::LRUCache<int, std::string> lru(CAPACITY);
    myCache::LFUCache<int, std::string> lfu(CAPACITY);
    myCache::ArcCache<int, std::string> arc(CAPACITY / 2);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::array<myCache::CachePolicy<int, std::string> *, 3> caches = {&lru, &lfu, &arc};
    std::vector<int> hits(3, 0);
    std::vector<int> get_operations(3, 0);

    for (int i = 0; i < caches.size(); ++i)
    {
        for (int op = 0; op < OPERATIONS; ++op)
        {
            int phase = op / PHASE_LENGTH;
            int putProbability; 
            // 模拟不同阶段的读写压力
            switch (phase) {
                case 0: putProbability = 15; break; // 阶段1：极高热点
                case 1: putProbability = 30; break; // 阶段2：大范围震荡
                case 2: putProbability = 10; break; // 阶段3：低频扫描
                default: putProbability = 20;
            }

            bool isPut = (gen() % 100 < putProbability);
            int key;
            // 动态调整访问模式生成 Key
            if (op < PHASE_LENGTH) key = gen() % 5; 
            else if (op < PHASE_LENGTH * 2) key = gen() % 400; 
            else if (op < PHASE_LENGTH * 3) key = (op % 100); 
            else {
                int r = gen() % 100;
                key = (r < 40) ? (gen() % 5) : (gen() % 350); 
            }

            if (isPut) caches[i]->put(key, "shift" + std::to_string(op));
            else {
                get_operations[i]++;
                std::string res;
                if (caches[i]->get(key, res)) hits[i]++;
            }
        }
    }
    printResults("工作负载剧烈变化测试", CAPACITY, get_operations, hits);
}

int main()
{
    testHotDataAccess();
    testLoopPattern();
    testWorkloadShift();
    return 0;
}