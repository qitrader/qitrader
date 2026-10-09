#include "actor_critic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
  initializeParameters();
}

void ActorCritic::initializeParameters() {
  // 小幅确定性初始化，保证回测结果可复现且初始动作接近均匀分配。
  m_actor_weights.assign(m_action_size, std::vector<double>(m_observation_size, 0.0));
  m_actor_bias.assign(m_action_size, 0.0);
  m_critic_weights.assign(m_observation_size, 0.0);
  m_critic_bias = 0.0;
  m_last_mean.clear();
  m_last_sample.clear();
  for (std::size_t action = 0; action < m_action_size; ++action) {
    for (std::size_t feature = 0; feature < m_observation_size; ++feature) {
      m_actor_weights[action][feature] =
          0.01 * std::sin(static_cast<double>((action + 1) * (feature + 1)));
    }
  }
}

void ActorCritic::reset() {
  initializeParameters();
}

void ActorCritic::setConfigFingerprint(const std::string& fingerprint) {
  m_config_fingerprint = fingerprint;
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
  // 首行带版本与配置指纹：指纹让"换档后的旧权重"在加载时被识别为不兼容。
  // 指纹为空写 '-'，保证字段数固定。
  out << "v1 " << m_observation_size << ' ' << m_action_size << ' '
      << (m_config_fingerprint.empty() ? std::string("-") : m_config_fingerprint) << '\n';
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

  // 首行可能是 "v1 <obs> <act> <fingerprint>"（新格式）或 "<obs> <act>"（旧格式）。
  // 旧格式无指纹，视为匹配，保证升级前的权重文件仍可用。
  std::string header;
  if (!(in >> header)) return false;
  if (header == "v1") {
    std::string fingerprint;
    if (!(in >> observation_size >> action_size >> fingerprint)) return false;
    if (fingerprint == "-") fingerprint.clear();
    // 双方都有指纹且不相等：配置档位变了（如 min_half_spread_bps 6 -> 24），
    // 旧权重的动作语义已失效，必须拒绝而不是静默复用。
    if (!fingerprint.empty() && !m_config_fingerprint.empty() &&
        fingerprint != m_config_fingerprint) {
      return false;
    }
  } else if (!header.empty() && header[0] == 'v') {
    return false;  // 未来版本，当前实现无法解析
  } else {
    observation_size = std::stoull(header);
    if (!(in >> action_size)) return false;
  }

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
  // 不能直接 trunc 打开目标文件：策略每 model_save_interval_steps 步就保存一次，
  // 写到一半被 kill / 掉电会留下半个文件，下次 load() 解析失败会退回初始权重，
  // 本次运行的在线学习成果全部丢失。改为先写同目录临时文件，再用 std::rename
  // 原子替换（同一目录内的 rename 是原子的）；失败时只留下 .tmp，原模型不受损。
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::out | std::ios::trunc);
    if (!out) return false;
    out << serialize();
    out.flush();
    if (!out) {
      out.close();
      std::remove(tmp_path.c_str());
      return false;
    }
  }
  if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
    std::remove(tmp_path.c_str());
    return false;
  }
  return true;
}

bool ActorCritic::load(const std::string& path) {
  std::ifstream in(path);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return deserialize(buffer.str());
}

}  // namespace strategy::multilevel
