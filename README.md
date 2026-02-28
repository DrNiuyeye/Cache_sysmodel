# Multi-Policy Cache System (C++ 高性能通用缓存系统)

这是一个基于 C++17 开发的轻量级、插件化缓存库。项目实现了从基础的 FIFO 到复杂的自适应算法 ARC 等多种策略，并提供了针对高并发场景的哈希分片优化。通过内置的基准测试工具，可以直观地对比各算法在不同工作负载下的命中率表现。

##  项目特性
- **泛型设计**：支持任意类型的 Key 和 Value。
- **模块化架构**：算法实现与基类解耦，易于扩展。
- **高并发支持**：提供基于分片锁（Sharding）的 Hash-LRU 和 Hash-LFU，降低锁竞争。
- **工业级算法**：包含 LRU-K 和 ARC 等能够有效对抗缓存污染的先进算法。

---

## 📂 目录结构说明

```text
myCacheProject/
├── src/
│   ├── Common/         # 公共基类 (CachePolicy) 与 核心数据结构
│   ├── FIFO/           # 先进先出算法
│   ├── LRU/            # LRU 相关 (包含标准 LRU, LRU-K, Hash-LRU)
│   ├── LFU/            # LFU 相关 (包含标准 LFU, Hash-LFU)
│   ├── ARC/            # 自适应缓存替换算法 (核心模块)
│   └── test.cpp        # 综合基准测试程序
└── CMakeLists.txt      # 自动化构建脚本

## 🚀 算法模块介绍

```text


