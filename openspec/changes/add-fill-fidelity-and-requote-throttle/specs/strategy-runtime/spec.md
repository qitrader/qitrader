## MODIFIED Requirements

### Requirement: 订单计划差异计算
订单管理器 SHALL 按"档位身份 + 价格相对容差"判定在途订单是否可复用：
身份（intent_id，或方向+档位+类型）一致、数量精确相等、价格偏差在相对容差内即视为同一档位；
计划指纹 SHALL 只由语义内容与作用域内活动订单集合构成，不包含每次递增的 plan_id；
REPLACE_ALL 策略 SHALL 跳过指纹短路，仍然全撤全挂。

#### Scenario: 未变化的档位被保留
- **WHEN** 策略提交的目标集合中某档位价格仅在容差内抖动
- **THEN** 差异为空，不产生撤单与新单

#### Scenario: 只有一档变化时只换该档
- **WHEN** 目标集合中只有一个档位的价格变化超过容差
- **THEN** 差异只包含一个撤单与一个新单，其余档位保留

#### Scenario: REPLACE_ALL 不被指纹短路
- **WHEN** 计划内容与在途订单一致但策略为 REPLACE_ALL
- **THEN** 仍然先撤掉全部在途订单再重新提交
