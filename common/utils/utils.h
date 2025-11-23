#ifndef __COMMON_UTILS_H
#define __COMMON_UTILS_H

/**
 * @file utils.h
 * @brief 通用工具函数和类型定义
 * 
 * 包含：
 * - 高精度浮点数类型定义
 * - 加密和时间工具函数
 * - 单例模式模板类
 * - JSON序列化支持
 */

#include <cstdint>
#include <string>
#include <boost/multiprecision/cpp_dec_float.hpp>
#include <boost/asio.hpp>
#include <fmt/format.h>
#include <jsoncpp/jsoncpp.hpp>

namespace asio = boost::asio;

/// 高精度浮点数类型，50位精度，用于金融计算
using dec_float = boost::multiprecision::cpp_dec_float_50;

/**
 * @brief 扩展jsoncpp库，支持dec_float类型的JSON转换
 */
namespace jsoncpp {

/**
 * @brief dec_float类型的JSON转换器
 */
template<>
struct transform<dec_float> {
    /**
     * @brief 将JSON值转换为dec_float
     * @param jv JSON值
     * @param t 目标dec_float变量
     * @throws std::runtime_error 如果类型不支持
     */
    static void trans(const bj::value &jv, dec_float &t) {
        if (jv.is_string()) {
            if (jv.as_string().empty()) {
                t = dec_float(0);
                return;
            }
            t = dec_float(jv.as_string().c_str());
        } else if (jv.is_double()) {
            t = dec_float(jv.as_double());
        } else if (jv.is_int64()) {
            t = dec_float(jv.as_int64());
        } else {
            throw std::runtime_error("invalid type");
        }
    }

    static bj::value to_json(const dec_float &t) {
        bj::value obj = t.str().c_str();
        return obj;
    }
};
}


namespace Common {

/**
 * @brief 计算SHA256哈希并返回Base64编码
 * @param input 输入字符串
 * @param key 密钥
 * @return std::string Base64编码的哈希值
 */
extern std::string sha256_hash_base64(const std::string& input, const std::string& key);

/**
 * @brief 获取当前时间戳（秒）
 * @return int64_t 当前时间戳
 */
extern int64_t get_current_time_s();

/**
 * @brief 将时间戳格式化为ISO 8601格式
 * @param time 时间戳（秒）
 * @return std::string ISO格式的时间字符串
 */
extern std::string time_format_iso(const int64_t& time);

/**
 * @brief 单例模式模板类
 * 
 * 提供线程安全的单例实现，使用智能指针管理对象生命周期。
 * C++11后的静态局部变量初始化保证线程安全。
 * 
 * @tparam T 单例类型
 */
template<typename T>
class SingletonPtr {
 public:
  /**
   * @brief 获取单例实例
   * @return std::shared_ptr<T> 单例对象的智能指针
   */
  static std::shared_ptr<T> get_instance() {
    static std::shared_ptr<T> instance = std::make_shared<T>();  // C++11后保证线程安全
    return instance;
  }

 private:
  SingletonPtr() {}  // 构造函数私有化
  SingletonPtr(const SingletonPtr&) = delete;              // 禁止拷贝构造
  SingletonPtr& operator=(const SingletonPtr&) = delete;   // 禁止赋值操作
};

}  // namespace Common

#endif
