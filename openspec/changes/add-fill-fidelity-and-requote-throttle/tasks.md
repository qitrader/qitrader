## 1. 撮合保真度
- [x] 1.1 新增 `FillModelConfig`（队列模型、fill_ratio、逆向选择滑点、退化成交量）
- [x] 1.2 挂单时估算前置排队量，成交需排队被主动成交量吃掉
- [x] 1.3 onTick 按行情时间戳幂等推进（回测一帧会调两次）
- [x] 1.4 补充 `tests/match_engine_test.cpp`（队列延迟成交、关闭即旧行为、幂等、滑点方向、无盘口退化）

## 2. 增量替换（降低撤单率）
- [x] 2.1 `OrderManager` 按档位身份建索引 + 价格相对容差匹配
- [x] 2.2 指纹改为语义内容 + 活动订单集合，REPLACE_ALL 不被短路
- [x] 2.3 策略报价按固定价格精度量化（步长与中价无关）
- [x] 2.4 新增重新报价阈值 `--mm-requote-threshold-bps`
- [x] 2.5 补充增量替换与 REPLACE_ALL 用例

## 3. 权重兼容性
- [x] 3.1 权重文件首行写入 `v1 <obs> <act> <fingerprint>`，旧格式仍可加载
- [x] 3.2 指纹不匹配时拒绝加载（6bps -> 24bps 场景）
- [x] 3.3 新增 `reset()`、`--mm-reset-model`、`--mm-eval-only`、`--model-load-path`
- [x] 3.4 补充指纹校验与 reset 用例

## 4. 运维与文档
- [x] 4.1 `paper_service.sh` stop 清理未登记的守护进程，并轮询等待优雅退出
- [x] 4.2 `paper_service.sh` 透传 `MM_REQUOTE_BPS`
- [x] 4.3 README 补充新参数

## 5. 验证
- [x] 5.1 单元测试 10/10 通过，smoke test 7/7 通过
- [x] 5.2 线上部署：撤单频率下降约 50%，出现完全复用的决策帧
- [ ] 5.3 录制文件按 UTC 日切（需跨 UTC 日后确认）
