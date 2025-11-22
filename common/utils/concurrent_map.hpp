#ifndef CONCURRENT_MAP_HPP
#define CONCURRENT_MAP_HPP

#include <map>
#include <functional>
#include <mutex>

template <typename K, typename V>
class ConcurrentMap {
public:
    V& operator[](const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_[key];
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

    V& at(const K& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.at(key);
    }

    void set(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        map_[key] = value;
    }

    void apply(std::function<void(std::map<K, V>&)> func) {
        std::lock_guard<std::mutex> lock(mutex_);
        func(map_);
    }

private:
    std::map<K, V> map_;
    std::mutex mutex_;
};

#endif // CONCURRENT_MAP_HPP
