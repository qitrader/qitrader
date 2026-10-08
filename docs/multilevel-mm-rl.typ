// Multi-Level Market Making with Reinforcement Learning —— 论文解读
// 编译：typst compile docs/multilevel-mm-rl.typ
#set document(
  title: "多层次做市与强化学习：论文解读",
  author: "qitrader",
)

#let accent = rgb("#1a4f8a")

#set page(
  paper: "a4",
  margin: (x: 2.2cm, y: 2.3cm),
  numbering: "1",
  header: context {
    if counter(page).get().first() > 1 [
      #set text(size: 8pt, fill: luma(110))
      #smallcaps[Multi-Level Market Making with Reinforcement Learning] #h(1fr) 论文解读
      #v(-2pt)
      #line(length: 100%, stroke: 0.4pt + luma(200))
    ]
  },
)

#set text(font: ("Noto Serif CJK SC", "Noto Sans CJK SC"), size: 10.5pt, lang: "zh", region: "cn")
#set par(justify: true, leading: 0.72em, first-line-indent: 0em)
#set heading(numbering: "1.1")
#show heading: it => {
  set text(fill: accent, weight: "bold")
  if it.level == 1 {
    block(above: 1.2em, below: 0.6em)[
      #text(size: 14.5pt, it.body)
      #v(-3pt)
      #line(length: 100%, stroke: 0.9pt + accent.lighten(60%))
    ]
  } else {
    block(above: 0.9em, below: 0.5em, it)
  }
}
#show raw: set text(font: ("Noto Sans Mono CJK SC", "DejaVu Sans Mono"), size: 8.8pt)

#set table(
  stroke: (x, y) => (
    top: if y == 0 { 0.9pt + accent } else { 0.4pt + luma(205) },
    bottom: 0.4pt + luma(205),
    left: none,
    right: none,
  ),
  fill: (x, y) => if y == 0 { accent.lighten(92%) } else if calc.odd(y) { luma(248) },
  inset: (x: 6pt, y: 4.5pt),
)
#show table: set text(size: 9pt)
#show figure.caption: set text(size: 8.5pt, fill: luma(80))

// 封面标题区
#align(center)[
  #v(1.2cm)
  #text(size: 21pt, weight: "bold", fill: accent)[
    Multi-Level Market Making with Reinforcement Learning
  ]
  #v(0.3em)
  #text(size: 13pt, fill: luma(60))[基于强化学习的多层次限价单簿做市 —— 论文解读]
  #v(1em)
  #text(size: 10pt, fill: luma(90))[
    Patrick Cheridito, Moritz Weiss（ETH Zurich） \
    arXiv:2608.18195 [q-fin.TR] · 2026-08-18
  ]
  #v(1.4cm)
]

#outline(title: [目录], indent: auto, depth: 2)
#pagebreak()

= 概览

== 一句话总结

把做市建模为 *有限期马尔可夫决策过程*：智能体在每个决策时刻输出一个单纯形上的连续向量，表示「预算 $M$ 手资金如何分配到市价单与多档限价单」，用 *logistic-正态分布* 作为策略、*deep-set 编码器* 处理变长挂单集合、*势能型奖励塑形* 加速训练，最终在多智能体泊松订单流模拟器中显著击败 TOP1 / TOP2 / INV 三个启发式基线。

== 论文元信息

#table(
  columns: (auto, 1fr),
  [标题], [Multi-Level Market Making with Reinforcement Learning],
  [作者], [Patrick Cheridito, Moritz Weiss],
  [机构], [Department of Mathematics, ETH Zurich],
  [分类], [q-fin.TR（交易与市场微观结构）],
  [关键词], [Trading, market making, limit order book, reinforcement learning],
  [方法族], [Actor-Critic 策略梯度 + Logistic-Normal 策略 + Deep Sets 编码],
)

== 三个主要贡献

+ *通用化的状态与动作空间*：状态包含完整订单簿（前 $K$ 档量）、自有挂单的档位/队列位置/数量、以及历史订单流；动作允许同时在多个价位、以不同规模提交市价单与限价单。
+ *面向做市的 actor-critic 算法*：logistic-正态分布实现灵活的资金分配，deep-set 编码器处理变长特征，势能型奖励塑形在不改变最优策略的前提下加速学习。
+ *带逆向选择的多主体模拟实验*：在噪声、战术、战略三类交易者构成的市场中系统评测，并分析 markout、动作分布、成交/撤单率与库存演化。

= 问题建模

交易区间为 $[0, T]$，在离散时刻 $t_n = n Delta t$ 决策，$n = 0, 1, dots, N-1$，其中 $Delta t = T\/N$。每个 $t_n$ 智能体观测状态 $s_n$、执行动作 $a_n$（先撤旧单再下新单），获得奖励 $r(s_n, a_n)$。

- *预算约束*：每个决策时刻最多投放 $M in NN$ 手（lot）到市价单与限价单，因此库存每步变化不超过 $M$。实验中 $M = 2$ 与 $M = 20$。
- *库存定义*：$Q_n$ 为 $[0, t_n]$ 内买单成交量与卖单成交量之差，初始 $Q_0 = 0$。
- *优化目标*：学习随机策略 $pi(a mid s)$，最大化

$ J(pi) = EE_(a tilde pi, s_0 tilde rho) [ sum_(n=0)^(N-1) r(s_n, a_n) + g(s_N) ] $

其中 $g(s_N)$ 为终端奖励（强制平仓带来的现金流）。

= 状态空间

状态分为「所有参与者可见的市场状态」与「仅智能体可知的私有状态」两部分。

== 市场状态（公开）

#table(
  columns: (1.1fr, 2fr),
  [*特征*], [*含义*],
  [最优买卖价 $p_n^b, p_n^a$], [报价基准，差值即 spread],
  [前 $K$ 档挂单量 $v_n^(b,k), v_n^(a,k)$], [供给/需求分布，隐含短期价格压力],
  [中间价变动 $Delta p_n$], [$p_n - p_(n-1)$，捕捉短期动量],
  [市场单流 $Delta_n^M$], [区间内市价买单量 − 市价卖单量],
  [限价单流 $Delta_n^L$], [区间内限价买单量 − 限价卖单量],
  [撤单流 $Delta_n^C$], [区间内限价买撤单量 − 限价卖撤单量],
)

== 私有状态

#table(
  columns: (1.1fr, 2fr),
  [*特征*], [*含义*],
  [时间 $t_n \/ T$], [归一化剩余时间，影响终端平仓紧迫度],
  [库存 $Q_n \/ M$], [当前净持仓],
  [挂单数量 $m_n^b, m_n^a$], [未成交买/卖限价单笔数（变长）],
  [挂单三元组 $(l_n^i, q_n^i, w_n^i)$], [档位 $l$（距最优价 tick 数）、队列位置 $q$（前方排队手数）、手数 $w$],
  [各档占比 $kappa_n in [0,1]^(2(K+1))$], [自有挂单在各档占 $M$ 的比例，第 $K+1$ 维为「更深档」汇总],
)

#block(
  fill: accent.lighten(94%),
  inset: 9pt,
  radius: 3pt,
  width: 100%,
)[
  *队列位置是核心特征*：它直接决定成交概率，也是做市的护城河。论文因此规定——撤单只在必要时进行，以尽量保留队列优先级。
]

= 动作空间

动作是单纯形 $SS^(2(K+1))$ 上的向量，$K = 3$ 时共 8 维，含义如下：

#table(
  columns: (auto, 1fr, auto, 1fr),
  [*分量*], [*含义*], [*分量*], [*含义*],
  [$a^0$], [不挂单（闲置）比例], [$a^(K+2)$], [市价卖单比例],
  [$a^1$], [市价买单比例], [$a^(K+3) dots a^(2(K+1))$], [卖方向第 $1 dots K$ 档限价单],
  [$a^2 dots a^(K+1)$], [买方向第 $1 dots K$ 档限价单], [],
)

连续比例需转为整数手数，论文采用 *Hamilton 最大余额法*：先取 $floor(a^k M)$ 的整数部分，再把剩余手数按小数部分从大到小依次补齐，保证总量恰为 $M$，避免舍入漂移导致预算浪费。

= 奖励函数

令 $overline(r)(s_n, a_n)$ 为 $(t_n, t_{n+1}]$ 内成交产生的现金流，$p_n$ 为中间价，则

$ r(s_n, a_n) = 1/M ( overline(r)(s_n, a_n) + underbrace((Q_(n+1) p_(n+1) - Q_n p_n), "势能项") - gamma abs(Q_(n+1)) ) $

- *势能项*：按中间价计价的持仓市值变化。整幕求和后相邻项相互抵消，因此 *不改变最优策略*（Ng et al., 1999 的 potential-based reward shaping），但把原本只在终端兑现的价格波动信号变成每步稠密反馈，显著加速信用分配。
- *库存惩罚* $-gamma abs(Q_(n+1))$：$gamma = 0.01$，抑制持仓风险。
- *终端奖励*：终端允许留仓 $abs(Q_(N+)) <= ceil(nu M)$，超出部分用市价单强制平仓，现金流记为 $"MO"_nu (s_N)$；剩余库存按中间价计价。实验默认 $nu = 0$（必须清空）。

$ g(s_N) = 1/M ( p_N (Q_(N+) - Q_N) + "MO"_nu (s_N) ) $

= 算法：三个技术核心

== Deep-Set 编码器：处理变长挂单集合

若把挂单 padding 成定长三元组向量，撤/挂单会导致同一张单在向量中「换槽位」，普通前馈网络会将其视为完全不同的输入。论文改用 *排列不变的集合编码*：对同一档位 $k$ 上的所有挂单，用共享小网络 $f_phi^o$ 嵌入后取平均。

$ f_(n,phi)^(e,b,k) = 1/abs(I_n^(b,k)) sum_(i in I_n^(b,k)) f_phi^o ( q_n^(b,i), w_n^(b,i) ), quad
  f_(n,phi)^(e,a,k) = 1/abs(I_n^(a,k)) sum_(i in I_n^(a,k)) f_phi^o ( q_n^(a,i), w_n^(a,i) ) $

其中 $I_n^(b,k) = { i : l_n^(b,i) = k }$，空集时对应编码置零。这样第 $k$ 个槽位 *永远对应第 $k$ 档*，语义稳定；总量信息由 $kappa$ 特征补齐；队列优先级通过 $q$ 进入 $f_phi^o$ 而得以保留。网络极轻量：单层 2 节点 + ReLU。

== Logistic-Normal 策略（Actor）

动作必须在单纯形上，因此对多元正态 $X tilde NN(mu_(theta^m)(s), Sigma_(theta^v))$ 做 logistic 变换：

$ a^k = e^(x^k) / (1 + sum_(l=1)^(2(K+1)) e^(x^l)) quad (k = 1, dots, 2(K+1)), quad quad
  a^0 = 1 / (1 + sum_(l=1)^(2(K+1)) e^(x^l)) $

- 均值由网络给出：$mu_(theta^m) = f_(theta^m)^m (f_phi^e (s))$；协方差为 *与状态无关的对角阵* $Sigma_(theta^v) = "Diag"(exp(theta^(v,1)), dots, exp(theta^(v,2(K+1))))$。
- 该分布有闭式密度，便于高效求梯度；且满足 $EE[log(a^j \/ a^k)] = mu^j - mu^k$。
- *初始化技巧*：输出层 bias 设为全 1、其余权重取 $10^(-5)$ 量级，使 $EE[log(a^k \/ a^0)] approx 1$，即开局就倾向下单，避免收敛到「永不交易」的退化解。
- 附录 C.1 与 *Dirichlet* 分布对比：LN 收敛更快更稳，三类市场期望现金流全面占优；在含战略交易者的市场中 DR 为 $-6.18$，LN 为 $+4.99$。

== Critic 与训练流程

价值网络 $V(s) = f_theta^V (f_phi^e (s))$；优势用整幕 return-to-go 减 baseline 估计：

$ A(s_n, a_n) = sum_(l=n)^(N-1) r(s_l, a_l) + g(s_N) - V(s_n) $

梯度步采用 *合并损失*，一次更新 encoder / actor / critic 三套参数（比拆成两步更高效）：

$ cal(L)(phi, theta, theta^V) =
  - 1/(tau N) sum_(k=1)^(tau) sum_(n=0)^(N-1) A(s_(n,k), a_(n,k)) log pi_(phi,theta)(a_(n,k) mid s_(n,k))
  + c_V dot 1/(tau N) sum_(k=1)^(tau) sum_(n=0)^(N-1) abs( V(s_(n,k)) - sum_(l=n)^(N-1) r(s_(l,k), a_(l,k)) - g(s_(N,k)) )^2 $

```
初始化 encoder φ、actor θ、critic ϑ，bias b，迭代数 H，轨迹数 τ，学习率 η，库存参数 γ，损失系数 c_V
for i = 1, ..., H:
    用当前策略 π_{φ_i, θ_i} 采样 τ 条完整轨迹 T_{N,τ}
    用 (上式) 对 (φ, θ, ϑ) 做一步 Adam 梯度更新
```

== 训练配置

#table(
  columns: (1fr, 1fr, 1fr, 1fr),
  [*超参*], [*取值*], [*超参*], [*取值*],
  [梯度步 $H$], [800], [轨迹数 $tau$], [1 280],
  [学习率 $eta$], [$5 times 10^(-4)$], [损失系数 $c_V$], [0.5],
  [库存参数 $gamma$], [0.01], [档位数 $K$], [3],
  [actor/critic 网络], [2 层 × 128，tanh], [编码器 $f_phi^o$], [1 层 × 2，ReLU],
  [优化器], [Adam],   [算力], [128 CPU + 单卡 24GB RTX],
)

= 市场模拟环境

订单到达均为泊松过程（Cont et al., 2010 框架），订单量服从半正态分布 $1 + delta abs(Z)$（$delta = 2$），簿深 $D = 30$ 档，起始价 1000 / 1001，仿真从订单簿「平均形态」出发。

#table(
  columns: (auto, 1fr, 1fr),
  [*交易者*], [*行为规则*], [*市场影响*],
  [噪声交易者], [市价/限价到达强度与状态无关，撤单强度 $prop$ 该档挂单量], [订单流基本对称、无方向性],
  [战术交易者], [强度 $prop$ 瞬时加权量失衡 $I_t^(plus.minus)$（$d^M = d^(L,k) = d^(C,k) = 4$）], [制造短促跳价],
  [战略交易者], [沿指数加权平滑信号 $macron(I)_t$（$beta = 0.1$，$z = 2$）方向交易], [制造持续趋势，是逆向选择主因],
)

其中加权量失衡定义为

$ V_t^b = sum_(k=1)^D v_t^(b,k) e^(-c(k-1)), quad V_t^a = sum_(k=1)^D v_t^(a,k) e^(-c(k-1)), quad
  I_t = (V_t^b - V_t^a) / (V_t^b + V_t^a) $

阻尼因子 $c = 0.65$ 控制深层挂单量的权重。加入战术/战略交易者后，噪声交易者强度分别下调 30% / 40% 以作补偿。

*Markout（成交后偏移）* 用于量化逆向选择：中间价在成交后朝不利方向移动即为被「摘单」。实验显示在含战略交易者的市场中，TOP1 挂 20 手的期望 markout 由 $+0.45$ 恶化为 $-0.06$，即最优档挂单被系统性逆向选择。

= 实验结果

评估指标为归一化现金流（剔除逐期库存惩罚，便于跨 $gamma$ 比较），10 000 条样本外测试回合：

$ 1/M ( sum_(n=0)^(N-1) overline(r)(s_n, a_n) + Q_(N+) p_N + "MO"_nu (s_N) ) $

#figure(
  table(
    columns: (1.5fr, auto, auto, auto, auto, auto),
    [*市场*], [$M$], [*TOP1*], [*TOP2*], [*INV*], [*LN*],
    [噪声], [2], [4.40], [2.91], [4.83], [*6.03*],
    [噪声], [20], [3.84], [1.48], [4.26], [*4.66*],
    [噪声 \+ 战术], [2], [5.47], [4.31], [5.11], [*9.11*],
    [噪声 \+ 战术], [20], [2.88], [2.55], [1.22], [*5.68*],
    [\+ 战略], [2], [3.95], [3.83], [3.24], [*8.55*],
    [\+ 战略], [20], [0.73], [2.05], [$-1.44$], [*4.99*],
  ),
  caption: [归一化现金流的期望值（标准差见原文表 2；LN 通常同时具有更低的标准差）],
)

关键结论：

- *环境越「有毒」，LN 优势越大*：在含战略交易者的市场中，M=20 时 INV 已亏损 $-1.44$，而 LN 仍有 $+4.99$，且标准差仅 1.07。
- *简单 skew 不再奏效*：INV 在存在战术交易者的 M=20 场景下滑落到基线最差，说明「库存控制必须叠加订单簿预测信号」。
- *消融 1（库存惩罚）*：$gamma = 0$ 期望更高但标准差暴涨（噪声市场 M=2：$sigma$ 从 2.81 → 8.20），说明静态库存惩罚在状态依赖市场中的作用有限，作者建议未来研究状态依赖的 $gamma$。
- *消融 2（终端约束）*：$nu = 0.5$（允许留仓按中间价计价）期望更高，因为省下了吃单平仓的价差成本。

= 学到的交易行为

+ *几乎不发市价单*：主动市价单占比仅 1.9% ~ 2.7%，且几乎全部来自终端强平。原因直白——市价单付 spread，限价单赚 spread。
+ *逆向选择越强，挂单越往深处放*：噪声市场几乎全挂最优档；加入战术交易者后开始用次优档；有战略交易者时，M=20 的成交单有 8.30% 挂在第三档（噪声+战术市场仅 2.10%）。
+ *主动管理在挂订单*：成交率从 74.65% 降至 43.99%，撤单率升至 56.01%，在有信息流的市场中策略明显更频繁地挪单。
+ *库存均值贴零、条件不对称*：整幕平均库存始终 $approx 0$，不靠单边偏仓获利；但条件于 $Q_n >= M\/2$ 时，量会明显 skew 到卖方——即状态依赖的倾斜报价。
+ *M 越大越分散*：M=2 时集中挂最优档，M=20 时分散到二三档，与「大单更容易被逆向选择」一致。

= 对 qitrader 的落地要点

仓库中的 `openspec/changes/add-multilevel-market-making/` 已规划该论文的落地，包含多档限价单与预算分配、轻量 Logistic-Normal actor-critic（不引入外部 ML 依赖）、Deep-Set 风格聚合与订单簿失衡特征、势能型库存奖励塑形、撤单事件通道等。实现时最值得照搬的几处细节：

#table(
  columns: (auto, 1fr),
  [*要点*], [*说明*],
  [Hamilton 取整], [保证分配总量恰为预算 $M$，避免逐档四舍五入造成预算浪费或超额],
  [队列位置入特征], [$q$ 决定成交概率；撤单策略必须「只在必要时撤」，否则会丢失队列优先级、被成交模型惩罚],
  [势能项不可省], [奖励必须包含 $Q_(n+1) p_(n+1) - Q_n p_n$，否则价格波动只在终端兑现，反馈稀疏、训练极慢],
  [状态归一化], [队列量除以 100、手数除以 $M$、时间除以 $T$、价格用相对收益，把特征压到 $[-1,1]$ 量级以稳定训练],
  [初始 bias 偏置], [让初始策略倾向下单而非闲置，规避「永不交易」的退化局部最优],
  [动作维度], [$K=3$ 时共 $2(K+1)=8$ 维（闲置 + 双边市价 + 双边 3 档限价），买卖两侧可不对称],
)

= 小结

这篇论文的实质贡献不在于提出新的 RL 算法，而在于 *把做市的动作空间做成了可微的连续分配问题*：用单纯形上的 logistic-正态分布统一表达「挂不挂、挂几档、挂多少、要不要吃单」，再用 deep-set 编码器消化变长挂单状态、用势能塑形解决奖励稀疏。在存在逆向选择的市场中，这种状态依赖的自适应报价相对固定档位启发式的优势会被显著放大——这恰恰是实盘做市最关心的场景。
