## ADDED Requirements

### Requirement: 报价量化与重新报价阈值
多层级做市策略 SHALL 按与中价无关的固定价格精度量化报价，并 SHALL 支持重新报价阈值：
目标价相对当前在途报价的偏移小于阈值时沿用旧报价，以压低撤单抖动。

#### Scenario: 中价小幅抖动不触发换单
- **WHEN** 新目标价相对在途报价的偏移小于 `--mm-requote-threshold-bps`
- **THEN** 沿用旧报价提交，不产生撤单

### Requirement: 权重文件版本与配置指纹
策略权重文件 SHALL 在首行写入版本号与配置指纹；配置指纹不匹配时 SHALL 拒绝加载并回退初始权重；
SHALL 支持 `--mm-reset-model` 从零重训、`--mm-eval-only` 只推理不更新、`--model-load-path`
单独指定读取路径。

#### Scenario: 换档后旧权重被拒绝
- **WHEN** 权重指纹为 `min_half_spread_bps=6` 而当前配置为 24
- **THEN** 加载失败，策略使用初始权重并记录告警

#### Scenario: 从零重训
- **WHEN** 指定 `--mm-reset-model`
- **THEN** 丢弃已有权重回到初始权重，不加载任何历史文件
