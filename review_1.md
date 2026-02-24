# librtdi 评审报告（review_1）

## 评审范围

- 需求基线： [REQUIREMENTS.md](REQUIREMENTS.md)（v2.1，2026-02-24）
- 代码与工程对象：当前仓库根项目（`include/`、`src/`、`tests/`、`examples/`、CMake/CPack）
- 评审目标：
  - 1）需求文档本身是否存在歧义、未说清、约束过严或缺漏
  - 2）在当前需求下，构建、实现、测试、打包、文档是否存在不符合项

---

## 一、需求文档质量评审（REQUIREMENTS v2.1）

结论：**存在若干需要收敛的规范问题**，建议先修订文档再做“严格合规冻结”。

### 1) 歧义/冲突点

- **Forward 覆盖范围表述易冲突**
  - 8.4 写“传播 Foo 的**所有已注册槽位**”，8.6 又写“仅展开 non-keyed”。
  - 读者容易理解成“所有槽位包含 keyed”，与 8.6 冲突。
  - 证据： [REQUIREMENTS.md](REQUIREMENTS.md#L323-L336)

- **并发语义粒度不清（once-per-type）**
  - 文档写 singleton 要 once-per-type；但 4-Slot 模型允许同一 type 有集合多 descriptor。
  - 这里应明确是 once-per-type、once-per-slot 还是 once-per-descriptor。
  - 证据： [REQUIREMENTS.md](REQUIREMENTS.md#L78-L96) 、 [REQUIREMENTS.md](REQUIREMENTS.md#L474-L480)

- **循环检测节点定义不充分**
  - 10.5 的节点按“接口类型”；但依赖匹配在 10.3 是三元组 `(type,is_collection,is_transient)`。
  - 应明确：环检测按 type 粒度（忽略 lifetime/collection）还是按槽位粒度。
  - 证据： [REQUIREMENTS.md](REQUIREMENTS.md#L407-L426)

### 2) 缺漏点

- **`registration_mismatch` 缺触发条件定义**
  - 文档定义了该异常及携带信息，但未说明在哪些场景必须抛出。
  - 证据： [REQUIREMENTS.md](REQUIREMENTS.md#L442-L455)

### 3) 可能过严/不精确表述

- **`unique_ptr<void, D>` 不合法** 的措辞过于绝对
  - 建议改为“默认 deleter 方案不可用/不适配本项目目标”，减少术语争议。
  - 证据： [REQUIREMENTS.md](REQUIREMENTS.md#L224-L229)

---

## 二、对照需求的项目符合性评审

## 2.1 构建与测试状态

- `ctest --test-dir build --output-on-failure`：**127/127 通过**。
- 说明：当前实现在现有测试集下稳定通过。

## 2.2 打包状态

- `cmake --build build --target package` 成功生成：
  - `build/librtdi-0.1.0-Linux.tar.gz`
  - `build/librtdi-0.1.0-Linux.deb`
- 说明：打包链路可用。

## 2.3 实现符合项（主要）

- **API 形态与新版需求主体一致**
  - 注册：`add_singleton` / `add_transient` / `add_collection`
  - 解析：`get` / `create` / `get_all` / `create_all`
  - 构建选项：`validate_on_build` / `validate_lifetimes` / `detect_cycles`
  - 证据： [include/librtdi/registry.hpp](include/librtdi/registry.hpp#L31-L409) 、 [include/librtdi/resolver.hpp](include/librtdi/resolver.hpp#L27-L132)

- **build 执行顺序符合需求**
  - forward 展开 → decorator 应用 → validation
  - 证据： [src/registry.cpp](src/registry.cpp#L169-L273)

- **校验流程与开关符合需求**
  - missing → lifetime（可关）→ cycle（可关）
  - 证据： [src/validation.cpp](src/validation.cpp#L141-L148)

- **类型擦除与继承模型支持到位**
  - `erased_ptr` 与 `make_erased_as` 实现存在
  - MI/VI/diamond + forward/decorator 相关测试覆盖全面
  - 证据： [include/librtdi/descriptor.hpp](include/librtdi/descriptor.hpp#L25-L95) 、 [tests/test_inheritance.cpp](tests/test_inheritance.cpp)

## 2.4 不符合/风险项

- **`registration_mismatch` 异常仅定义未落地**
  - 头文件和实现里有该类，但当前未见触发路径与对应测试。
  - 若按需求“必须携带信息”且可触发，则当前为缺口。
  - 证据： [include/librtdi/exceptions.hpp](include/librtdi/exceptions.hpp#L94-L104) 、 [src/exceptions.cpp](src/exceptions.cpp#L114-L120)

- **并发语义与“once-per-type”字面可能不一致**
  - 实现以 descriptor index 缓存 singleton，更接近 once-per-descriptor/slot。
  - 若需求坚持严格 once-per-type，需要进一步定义并调整。
  - 证据： [src/resolver.cpp](src/resolver.cpp#L32-L33) 、 [src/resolver.cpp](src/resolver.cpp#L74-L97)

## 2.5 文档不符合（显著）

- 当前 README 仍为旧架构描述（`shared_ptr<void>`、`scoped`、`registration_policy`、`resolve_any/create_scope` 等），与 v2.1 明显不一致。
- 这会误导用户并影响对外契约一致性。
- 证据： [README.md](README.md#L5-L7) 、 [README.md](README.md#L115-L133) 、 [README.md](README.md#L329-L337)

---

## 三、总体判断

- 从“代码 + 测试 + 打包”看，当前主实现已较好贴合 v2.1 核心设计。
- 从“规范完备性”看，需求文档仍有需收敛条目（尤其 forward 范围、并发粒度、registration_mismatch 触发条件）。
- 从“对外交付一致性”看，**README 明显落后于实现和需求**，是当前最直观的不符合项。

---

## 四、建议与优先级

- **P0（先做）**：修订 [REQUIREMENTS.md](REQUIREMENTS.md) 的 5 处规范歧义/缺漏
  - forward 传播范围与 non-keyed 限制统一表述
  - once-per-type 粒度定义
  - 环检测节点粒度定义
  - registration_mismatch 触发条件
  - `unique_ptr<void, D>` 表述精炼

- **P1**：处理 `registration_mismatch`
  - 二选一：实现触发逻辑并补测试，或从需求与代码移除该异常

- **P1**：重写 [README.md](README.md) 为 v2.1 语义
  - 全量替换旧 API/生命周期/策略内容

- **P2**：并发语义与测试名词统一
  - 若采用 once-per-slot（建议），在需求与测试注释中显式写清

---

## 五、评审结论摘要（供决策）

- 问题 1（需求是否有歧义/缺漏）：**有**，且影响合规判定边界。
- 问题 2（项目是否存在不符合项）：**有**，主要在文档一致性（README）与少量规范-实现边界（registration_mismatch/并发粒度）。
- 工程可运行性：**构建、测试、打包均通过**。
