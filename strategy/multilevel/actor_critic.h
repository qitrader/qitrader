#ifndef QITRADER_STRATEGY_MULTILEVEL_ACTOR_CRITIC_H_
#define QITRADER_STRATEGY_MULTILEVEL_ACTOR_CRITIC_H_

#include <cstddef>
#include <random>
#include <string>
#include <vector>

namespace strategy::multilevel {

/**
 * @brief 无外部机器学习依赖的轻量级 Actor-Critic 策略。
 *
 * Actor 输出高斯潜变量，经 softmax 后形成 Logistic-Normal 动作；
 * Critic 使用线性价值函数，适合在事件驱动回测中在线更新。
 */
class ActorCritic {
 public:
  ActorCritic(std::size_t observation_size, std::size_t action_size,
              double learning_rate, double exploration);

  /// 根据当前观测采样单纯形上的动作比例。
  std::vector<double> sample(const std::vector<double>& observation);

  /// 使用一步 TD 误差更新 Actor 和 Critic。
  void update(const std::vector<double>& observation,
              const std::vector<double>& action,
              double reward,
              const std::vector<double>& next_observation,
              bool terminal);

  std::size_t observationSize() const { return m_observation_size; }
  std::size_t actionSize() const { return m_action_size; }

  /// 将全部可学习参数序列化为文本，便于落盘与版本管理。
  std::string serialize() const;

  /// 从文本恢复参数；维度不匹配或解析失败时返回 false，且不改动现有参数。
  bool deserialize(const std::string& data);

  /// 保存参数到文件。
  bool save(const std::string& path) const;

  /// 从文件加载参数。
  bool load(const std::string& path);

  /// 回到构造时的确定性初始化，丢弃全部在线学习成果（用于从零重训）。
  /// 只重置可学习参数，不重置配置指纹与随机数引擎。
  void reset();

  /// 设置配置指纹（如 "levels=3,min_half_spread_bps=24"）。
  /// 指纹会写入权重文件首行；加载时指纹不匹配的文件会被拒绝，
  /// 避免"参数换档后仍静默复用失效权重"（6bps -> 24bps 就属于这种情况）。
  void setConfigFingerprint(const std::string& fingerprint);

  const std::string& configFingerprint() const { return m_config_fingerprint; }

 private:
  /// 构造函数与 reset() 共用的确定性初始化
  void initializeParameters();
  std::vector<double> logits(const std::vector<double>& observation) const;
  std::vector<double> softmax(const std::vector<double>& values) const;

  std::size_t m_observation_size;
  std::size_t m_action_size;
  double m_learning_rate;
  double m_exploration;
  double m_discount{0.99};
  std::vector<std::vector<double>> m_actor_weights;
  std::vector<double> m_actor_bias;
  std::vector<double> m_critic_weights;
  double m_critic_bias{0};
  std::mt19937 m_random;
  std::normal_distribution<double> m_normal;

  /// 最近一次采样时的均值 μ 与采样值 z，供 update 计算 score function。
  /// Logistic-Normal 的策略梯度要用 (z - μ)，而不是 softmax 空间里的差值。
  std::vector<double> m_last_mean;
  std::vector<double> m_last_sample;

  /// 配置指纹，写入权重文件首行用于兼容性校验
  std::string m_config_fingerprint;
};

}  // namespace strategy::multilevel

#endif  // QITRADER_STRATEGY_MULTILEVEL_ACTOR_CRITIC_H_
