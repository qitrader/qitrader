#ifndef QITRADER_COMMON_UTILS_CONCURRENT_MAP_HPP_
#define QITRADER_COMMON_UTILS_CONCURRENT_MAP_HPP_

#include <map>
#include <functional>
#include <mutex>
#include <optional>

template <typename K, typename V>
class ConcurrentMap {
public:
    /// 获取值的拷贝，若 key 不存在则返回默认值
    V get(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return V{};
    }

    /// 获取值的拷贝，若 key 不存在则返回 std::nullopt
    std::optional<V> try_get(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool contains(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.find(key) != map_.end();
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
    }

    void set(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        map_[key] = value;
    }

    /// 在锁保护下对整个 map 执行操作（引用传递）
    void apply(std::function<void(std::map<K, V>&)> func) {
        std::lock_guard<std::mutex> lock(mutex_);
        func(map_);
    }

    /// 在锁保护下对单个 key 对应的值执行操作
    void apply_at(const K& key, std::function<void(V&)> func) {
        std::lock_guard<std::mutex> lock(mutex_);
        func(map_[key]);
    }

private:
    std::map<K, V> map_;
    std::mutex mutex_;
};

#endif // CONCURRENT_MAP_HPP
