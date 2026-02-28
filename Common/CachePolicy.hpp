#ifndef __CACHEPOLICY_HPP__
#define __CACHEPOLICY_HPP__

#include <iostream>

namespace myCache
{
    /**
     * @brief 缓存策略抽象基类
     * 定义了所有缓存实现必须遵循的接口规范
     */
    template <class Key, class Value>
    class CachePolicy
    {
    public:
        // 虚析构函数：确保子类对象能被正确销毁，防止内存泄漏
        virtual ~CachePolicy() {};

        /**
         * @brief 存入缓存
         * 将键值对(Key-Value)放入缓存中
         */
        virtual void put(Key key, Value value) = 0;

        /**
         * @brief 读取缓存
         * @param key 要查找的键
         * @param value 如果找到，通过此参数传出结果
         * @return 命中返回 true，未找到返回 false
         */
        virtual bool get(Key key, Value& value) = 0;

        /**
         * @brief 读取缓存
         * @param key 要查找的键
         * @return 直接返回对应的值；若找不到，则返回该类型的默认值
         */
        virtual Value get(Key key) = 0;
    };
}

#endif