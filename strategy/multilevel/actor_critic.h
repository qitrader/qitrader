#ifndef QITRADER_STRATEGY_MULTILEVEL_ACTOR_CRITIC_H_
#define QITRADER_STRATEGY_MULTILEVEL_ACTOR_CRITIC_H_

#include <cstddef>
#include <random>
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

 private:
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
};

}  // namespace strategy::multilevel

#endif  // QITRADER_STRATEGY_MULTILEVEL_ACTOR_CRITIC_H_
