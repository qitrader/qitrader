#include "actor_critic.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

namespace strategy::multilevel {

ActorCritic::ActorCritic(std::size_t observation_size, std::size_t action_size,
                         double learning_rate, double exploration)
    : m_observation_size(observation_size),
      m_action_size(action_size),
      m_learning_rate(learning_rate),
      m_exploration(exploration),
      m_actor_weights(action_size, std::vector<double>(observation_size, 0.0)),
      m_actor_bias(action_size, 0.0),
      m_critic_weights(observation_size, 0.0),
      m_random(42),
      m_normal(0.0, 1.0) {
  // 小幅确定性初始化，保证回测结果可复现且初始动作接近均匀分配。
  for (std::size_t action = 0; action < m_action_size; ++action) {
    for (std::size_t feature = 0; feature < m_observation_size; ++feature) {
      m_actor_weights[action][feature] =
          0.01 * std::sin(static_cast<double>((action + 1) * (feature + 1)));
    }
  }
}

std::vector<double> ActorCritic::logits(const std::vector<double>& observation) const {
  std::vector<double> values(m_action_size, 0.0);
  for (std::size_t action = 0; action < m_action_size; ++action) {
    values[action] = m_actor_bias[action];
    for (std::size_t feature = 0;
         feature < std::min(m_observation_size, observation.size()); ++feature) {
      values[action] += m_actor_weights[action][feature] * observation[feature];
    }
  }
  return values;
}

std::vector<double> ActorCritic::softmax(const std::vector<double>& values) const {
  if (values.empty()) return {};
  const double maximum = *std::max_element(values.begin(), values.end());
  std::vector<double> result(values.size(), 0.0);
  double total = 0.0;
  for (std::size_t i = 0; i < values.size(); ++i) {
    result[i] = std::exp(std::clamp(values[i] - maximum, -40.0, 40.0));
    total += result[i];
  }
  if (total <= std::numeric_limits<double>::epsilon()) {
    const double uniform = 1.0 / static_cast<double>(values.size());
    std::fill(result.begin(), result.end(), uniform);
    return result;
  }
  for (double& value : result) value /= total;
  return result;
}

std::vector<double> ActorCritic::sample(const std::vector<double>& observation) {
  m_last_mean = logits(observation);
  m_last_sample = m_last_mean;
  for (double& value : m_last_sample) value += m_exploration * m_normal(m_random);
  return softmax(m_last_sample);
}

void ActorCritic::update(const std::vector<double>& observation,
                         const std::vector<double>& action,
                         double reward,
                         const std::vector<double>& next_observation,
                         bool terminal) {
  if (observation.size() != m_observation_size || action.size() != m_action_size) return;

  const auto current_probabilities = softmax(logits(observation));

  double current_value = m_critic_bias;
  for (std::size_t feature = 0; feature < m_observation_size; ++feature) {
    current_value += m_critic_weights[feature] * observation[feature];
  }

  double next_value = m_critic_bias;
  for (std::size_t feature = 0;
       feature < std::min(m_observation_size, next_observation.size()); ++feature) {
    next_value += m_critic_weights[feature] * next_observation[feature];
  }
  next_value = terminal ? 0.0 : next_value;
  const double td_error = reward + m_discount * next_value - current_value;
  const double bounded_error = std::clamp(td_error, -10.0, 10.0);

  for (std::size_t feature = 0; feature < m_observation_size; ++feature) {
    m_critic_weights[feature] += m_learning_rate * bounded_error * observation[feature];
  }
  m_critic_bias += m_learning_rate * bounded_error;

  // Logistic-Normal 的 score function 是 (z - μ)，z 为实际采样值、μ 为均值。
  // 此前这里用 softmax 空间的 action - prob：exploration 为 0 时两者恒等，
  // 梯度恒为零，策略永远不更新；非零时也只是在放大噪声而非真正的策略梯度。
  const bool has_sample =
      m_last_mean.size() == m_action_size && m_last_sample.size() == m_action_size;
  for (std::size_t action_index = 0; action_index < m_action_size; ++action_index) {
    const double score = has_sample
        ? m_last_sample[action_index] - m_last_mean[action_index]
        : action[action_index] - current_probabilities[action_index];
    const double policy_gradient =
        std::clamp(score * bounded_error, -1.0, 1.0);
    for (std::size_t feature = 0; feature < m_observation_size; ++feature) {
      m_actor_weights[action_index][feature] +=
          m_learning_rate * policy_gradient * observation[feature];
    }
    m_actor_bias[action_index] += m_learning_rate * policy_gradient;
  }
}

std::string ActorCritic::serialize() const {
  std::ostringstream out;
  out.precision(17);
  out << m_observation_size << ' ' << m_action_size << '\n';
  for (double value : m_actor_bias) out << value << ' ';
  out << '\n';
  for (const auto& row : m_actor_weights) {
    for (double value : row) out << value << ' ';
    out << '\n';
  }
  out << m_critic_bias << '\n';
  for (double value : m_critic_weights) out << value << ' ';
  out << '\n';
  return out.str();
}

bool ActorCritic::deserialize(const std::string& data) {
  std::istringstream in(data);
  std::size_t observation_size = 0;
  std::size_t action_size = 0;
  if (!(in >> observation_size >> action_size)) return false;
  // 维度必须一致：特征数随 levels 变化，加载旧模型会得到无意义的动作。
  if (observation_size != m_observation_size || action_size != m_action_size) return false;

  std::vector<double> actor_bias(action_size);
  for (std::size_t i = 0; i < action_size; ++i) {
    if (!(in >> actor_bias[i])) return false;
  }

  std::vector<std::vector<double>> actor_weights(action_size, std::vector<double>(observation_size));
  for (std::size_t a = 0; a < action_size; ++a) {
    for (std::size_t o = 0; o < observation_size; ++o) {
      if (!(in >> actor_weights[a][o])) return false;
    }
  }

  double critic_bias = 0.0;
  if (!(in >> critic_bias)) return false;

  std::vector<double> critic_weights(observation_size);
  for (std::size_t o = 0; o < observation_size; ++o) {
    if (!(in >> critic_weights[o])) return false;
  }

  // 全部解析成功后才提交，避免中途失败留下残缺参数。
  m_actor_bias = std::move(actor_bias);
  m_actor_weights = std::move(actor_weights);
  m_critic_bias = critic_bias;
  m_critic_weights = std::move(critic_weights);
  return true;
}

bool ActorCritic::save(const std::string& path) const {
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  if (!out) return false;
  out << serialize();
  return static_cast<bool>(out);
}

bool ActorCritic::load(const std::string& path) {
  std::ifstream in(path);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return deserialize(buffer.str());
}

}  // namespace strategy::multilevel
