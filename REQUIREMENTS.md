# 需求规格说明书

**项目名称**：librtdi — C++20 运行时依赖注入框架（MVP）  
**文档版本**：1.0  
**语言**：C++20  
**最后更新**：2026-02-19

---

## 目录

1. [背景与目标](#1-背景与目标)
2. [核心概念](#2-核心概念)
3. [组件描述符](#3-组件描述符)
4. [注册 API](#4-注册-api)
5. [生命周期语义](#5-生命周期语义)
6. [解析 API](#6-解析-api)
7. [作用域（Scope）](#7-作用域scope)
8. [注册策略](#8-注册策略)
9. [转发注册（Forward）](#9-转发注册forward)
10. [装饰器（Decorator）](#10-装饰器decorator)
11. [构建期校验](#11-构建期校验)
12. [异常体系](#12-异常体系)
13. [线程安全](#13-线程安全)
14. [明确的非目标](#14-明确的非目标)

---

## 1. 背景与目标

### 1.1 动机

提供一个参照 .NET `Microsoft.Extensions.DependencyInjection` 设计语义，适用于 C++20 的**运行时**依赖注入框架，满足以下需求：

- 无需入侵式基类、无需通过宏声明依赖
- 在一个统一的注册 → 构建 → 解析流程中管理服务的生命周期
- 在 `build()` 阶段而非运行时的首次解析时发现配置错误

### 1.2 核心设计目标

| 目标 | 说明 |
|------|------|
| 运行时类型擦除 | 所有注册信息以运行时类型而非模板参数为 key 存储，支持大规模注册 |
| 零宏 | 依赖声明通过语言原生手段完成，不引入预处理器宏 |
| 构建期校验 | 依赖图的正确性（缺失、循环、生命周期违规）在 `build()` 时全部检测完毕 |
| 可组合性 | 转发、装饰、多实现等机制可任意组合，无特殊顺序限制 |
| 人因工程 | 错误消息应包含 demangled 类型名与源码位置，便于诊断 |

---

## 2. 核心概念

### 2.1 框架整体流程

框架分为**三个阶段**，各阶段有明确边界：

```
注册阶段 (registry)
  └─ add_singleton / add_scoped / add_transient / forward / decorate
        ↓
构建阶段 (registry::build)
  └─ 校验依赖图 → 展开 forward → 应用 decorator → 生成 resolver
        ↓
解析阶段 (resolver)
  └─ resolve / resolve_any / resolve_all / create_scope
```

`registry::build()` 调用后，registry 本身的 descriptor 集合被移交给 resolver，registry 不再持有有效状态（仍可复用，但等同于空的新 registry）。

### 2.2 接口与实现的关系

- 每一条注册均绑定一个**接口类型**（用于查找）和一个**具体实现类型**（用于构造）
- 具体实现类型必须派生自接口类型（编译期 `is_base_of` 断言）
- 当 `TInterface == TImpl` 时，注册的是具体类型自身

### 2.3 依赖的表达方式

依赖通过一个零大小的**标签类型** `deps<D1, D2, ...>` 在注册点声明，传入注册方法作为参数：

```cpp
registry.add_singleton<IFoo, FooImpl>(deps<IBar, IBaz>);
```

`deps<>` 要求：
- `TImpl` 必须可由 `(shared_ptr<D1>, shared_ptr<D2>, ...)` 构造（编译期断言）
- 解析时框架对每个 `Di` 执行**严格解析**（即 `resolve<Di>()`，见第 6 节）

无依赖时直接调用不带标签的重载：

```cpp
registry.add_singleton<IFoo, FooImpl>();
// TImpl 必须默认可构造
```

---

## 3. 组件描述符

描述符（`descriptor`）是注册信息的运行时表示，属于**值类型**，包含以下语义字段：

| 字段 | 类型 | 语义 |
|------|------|------|
| `component_type` | `type_index` | 接口类型，查找 key 的一部分 |
| `lifetime` | `lifetime_kind` | 生命周期枚举 |
| `factory` | `function<shared_ptr<void>(resolver&)>` | 工厂函数，返回类型擦除的实例指针 |
| `dependencies` | `vector<type_index>` | `deps<>` 声明的依赖类型列表（用于 build-time 校验） |
| `key` | `string` | 空字符串表示非命名注册；非空为 keyed 注册 |
| `is_single_slot` | `bool` | 该 (component_type, key) 槽位是否被 `single` 策略锁定 |
| `impl_type` | `optional<type_index>` | 具体实现类型（用于装饰器精确匹配） |
| `forward_target` | `optional<type_index>` | 非空时表示这是一条 forward 占位符注册 |
| `forward_cast` | 函数对象 | forward 展开时的类型转换钩子（void\* → target type → interface type） |

描述符不对外暴露构造接口；用户只能通过 `registry::descriptors()` 以只读方式访问（用于测试和诊断）。

---

## 4. 注册 API

### 4.1 注册方法矩阵

registry 提供以下注册方法，所有方法均返回 `registry&`（支持链式调用），所有方法的最后一个参数均为可选的 `registration_policy`（默认 `multiple`）：

**非命名注册（non-keyed）：**

| 方法签名 | 生命周期 | 依赖来源 |
|----------|----------|----------|
| `add_singleton<I,T>()` | singleton | 无（T 默认可构造） |
| `add_singleton<I,T>(deps<D...>)` | singleton | deps<> 标签 |
| `add_scoped<I,T>()` | scoped | 无 |
| `add_scoped<I,T>(deps<D...>)` | scoped | deps<> 标签 |
| `add_transient<I,T>()` | transient | 无 |
| `add_transient<I,T>(deps<D...>)` | transient | deps<> 标签 |

**命名注册（keyed）：**

以上所有方法均存在对应的 keyed 重载，通过在参数列表首位增加 `string_view key` 区分。keyed 与 non-keyed 注册在同一接口类型下互不干扰。

### 4.2 编译期约束

每个 `add_*<I, T>` 调用均在编译期检查：

- `T` 派生自 `I`（`is_base_of<I, T>`）
- 无依赖版本：`T` 默认可构造
- 带依赖版本：`T` 可由 `(shared_ptr<D1>, ..., shared_ptr<Dn>)` 构造

违反约束将产生 `static_assert` 编译错误，错误消息需说明原因。

### 4.3 工厂函数语义

每条注册在内部持有一个工厂函数，签名为 `shared_ptr<void>(resolver&)`：

- 工厂在解析时被调用，接收当前激活的 `resolver`（可能为 scoped resolver）
- 工厂返回类型擦除的 `shared_ptr<void>`；框架在类型安全边界处通过 `static_pointer_cast<T>` 恢复类型
- 工厂执行时若抛出异常，框架应将其包装为 `resolution_error` 再向上传播

---

## 5. 生命周期语义

### 5.1 三种生命周期

| 枚举值 | 含义 |
|--------|------|
| `lifetime_kind::singleton` | 全局唯一。在根 resolver 上首次解析时创建；此后所有解析（包括任意 scope 内）均返回同一实例 |
| `lifetime_kind::scoped` | scope 内唯一。在每一个 scope 内首次解析时创建，同 scope 内的后续解析返回同一实例，不同 scope 返回不同实例 |
| `lifetime_kind::transient` | 每次解析均创建新实例；不缓存 |

### 5.2 缓存位置

- Singleton 实例由**根 resolver** 持有
- 从 scoped resolver 解析 singleton 时，返回根 resolver 上缓存的实例
- Scoped 实例由**对应 scope 的 resolver** 持有

### 5.3 非法场景

**从根 resolver 直接解析 scoped 组件**是错误操作，应抛出 `no_active_scope`。

### 5.4 懒初始化

Singleton 实例在首次被请求时才调用工厂创建，而非在 `build()` 时预创建。

---

## 6. 解析 API

### 6.1 Resolver 是只读的

resolver 在 `build()` 后不再接受注册操作，其接口仅提供解析方法。

### 6.2 非命名解析方法

| 方法 | 行为 |
|------|------|
| `resolve<T>()` | **严格解析**：恰好 1 条 non-keyed 注册时返回；0 条抛 `not_found`；>1 条抛 `ambiguous_component` |
| `try_resolve<T>()` | **严格解析（nullable）**：0 条返回 `nullptr`；>1 条抛 `ambiguous_component` |
| `resolve_any<T>()` | **宽松解析（last-wins）**：返回最后注册的 non-keyed 实现；0 条抛 `not_found` |
| `try_resolve_any<T>()` | **宽松解析（nullable）**：0 条返回 `nullptr` |
| `resolve_all<T>()` | 按注册顺序返回所有 non-keyed 实现的 vector；无注册时返回空容器（不抛异常） |

### 6.3 命名解析方法

与非命名解析方法一一对应，增加 `string_view key` 参数。keyed 解析仅匹配相同 key 的注册；non-keyed 与 keyed 注册之间互不可见。

### 6.4 "最后注册"的定义

`resolve_any` 返回的"最后注册"指注册调用在时间序上最晚的那一条，即 `descriptors()` 中索引最大的那条匹配项。

### 6.5 `deps<>` 在解析时的行为

`deps<Di...>` 中每个 `Di` 在工厂执行时调用 `resolver.resolve<Di>()`——即严格解析，若 `Di` 没有恰好 1 条 non-keyed 注册则抛异常。这个行为在 build-time 校验阶段也会提前检测（见 11.3、11.4 节）。

---

## 7. 作用域（Scope）

### 7.1 Scope 的创建

通过根 resolver 的 `create_scope()` 方法创建一个 `scope` 对象。`scope` 是 RAII 包装器，析构时释放其持有的所有 scoped 实例。

### 7.2 Scope 内的解析规则

通过 `scope::get_resolver()` 获取 scoped resolver 的引用，在其上调用解析方法：

- 解析 **singleton**：返回根 resolver 缓存的实例
- 解析 **scoped**：返回该 scope 内的缓存实例（首次时调用工厂创建）
- 解析 **transient**：每次都创建新实例

### 7.3 Scope 的生命周期约束

- `scope` 对象析构后，其持有的所有 scoped 实例被释放
- 框架不提供嵌套 scope——scoped resolver 不对外提供 `create_scope()`
- 一个根 resolver 可同时存在多个独立 scope，各 scope 之间互不影响

---

## 8. 注册策略

### 8.1 策略枚举

`registration_policy` 控制同一 `(component_type, key)` 槽位（以下简称"槽位"）的重复注册行为：

| 枚举值 | 行为 |
|--------|------|
| `multiple` | 默认值。直接追加，允许同一槽位有多条注册 |
| `single` | 锁定语义（详见 8.2 节） |
| `replace` | 移除该槽位所有已有注册后追加新注册（可覆盖 `single` 的锁定） |
| `skip` | 若该槽位已有任何注册则静默跳过，否则追加 |

### 8.2 `single` 策略的精确语义

`single` 的意图是"保证最终只有一个实现来响应严格解析"，行为取决于槽位当前状态：

| 槽位状态 | `single` 的行为 |
|----------|-----------------|
| 空 | 追加新注册，设 `is_single_slot = true` |
| 恰好 1 条注册且 `is_single_slot == false` | 仅设 `is_single_slot = true`，**不新增**注册 |
| 恰好 1 条注册且 `is_single_slot == true` | 抛 `duplicate_registration` |
| > 1 条注册 | 抛 `duplicate_registration` |

槽位被锁定（`is_single_slot == true`）后：

- 后续 `multiple` 或 `single` 调用抛 `duplicate_registration`
- `replace` 可强制覆盖（清除旧条目，追加新条目，新条目 `is_single_slot = false`）
- `skip` 静默跳过

### 8.3 策略判定粒度

策略判定以 `(component_type, key)` 为粒度，不同 key 之间的注册互不影响。

---

## 9. 转发注册（Forward）

### 9.1 目的

允许一个具体实现 `T` 同时以多个接口类型被解析，且多个接口共享**同一实例**（共享 `shared_ptr` 引用计数）。

### 9.2 注册时语义

`registry.forward<IBar, Foo>()` 要求 `Foo` 派生自 `IBar`（编译期断言）。在 descriptor 集合中追加一条**占位符 descriptor**（`forward_target = typeid(Foo)`），本身没有真实工厂；`build()` 时展开。

### 9.3 `build()` 阶段的展开规则

1. 遍历所有 `forward_target == typeid(T)` 的占位符
2. 对每条占位符，查找所有 `component_type == typeid(T)` 且 non-keyed 的真实 descriptor（按注册顺序）
3. 为每条目标 descriptor `d` 生成一条新 descriptor：
   - `component_type = typeid(IBar)`
   - `lifetime` 复制自 `d.lifetime`
   - `factory`：解析 `T` 得到实例，通过 `forward_cast` 将指针从 `T*` 调整为 `IBar*` 后返回
4. 用生成的 N 条 descriptor 替换原占位符

### 9.4 指针转换的正确性要求

`forward_cast` 必须处理多重继承的指针偏移：必须先将 `shared_ptr<void>` 还原为 `shared_ptr<T>`（中间类型），再转换为 `shared_ptr<IBar>`，不能直接从 `void*` 强转。

### 9.5 Forward 与生命周期校验

展开后的 `IBar` descriptor 的 lifetime 与对应的 `T` descriptor 完全一致，因此对 `IBar` 的依赖适用相同的生命周期兼容性规则。

### 9.6 当前限制

`forward<I, T>()` 仅对 `T` 的 non-keyed 注册展开；keyed 形式不在本 MVP 范围内。

---

## 10. 装饰器（Decorator）

### 10.1 目的

在不修改原始注册、不影响消费方的前提下，为一个或多个已注册实现透明地包裹额外逻辑（装饰器模式）。

### 10.2 装饰器注册时机

装饰器注册**不立即修改 descriptor 集合**，在内部维护一个待应用的 decorator 回调列表；`build()` 时统一应用。

### 10.3 四种注册形式

| 形式 | 匹配范围 | 构造约束 |
|------|----------|----------|
| `decorate<I, D>()` | `I` 的所有注册（keyed + non-keyed） | `D(shared_ptr<I>)` 可构造 |
| `decorate<I, D>(deps<Extra...>)` | 同上 | `D(shared_ptr<I>, shared_ptr<Extra>...)` 可构造 |
| `decorate<I, D>(type_index target)` | `impl_type == target` 的注册 | `D(shared_ptr<I>)` 可构造 |
| `decorate<I, D>(type_index target, deps<Extra...>)` | 同上 | `D(shared_ptr<I>, shared_ptr<Extra>...)` 可构造 |

编译期要求：`D` 派生自 `I`；构造签名如上。

### 10.4 `build()` 阶段的应用规则

按装饰器的注册顺序逐一处理：

- **全局装饰器**（无 target）：`d.component_type == typeid(I)` 时应用
- **目标装饰器**：`d.component_type == typeid(I)` 且 `d.impl_type == target` 时应用
- **应用方式**：`d.factory = decorator_wrapper(d.factory)`

descriptor 的 `impl_type` 不会因装饰而改变（精确匹配仍基于原始实现类型）。

### 10.5 链式叠加顺序

多个 decorator 按注册顺序叠加：先注册者在内层（更靠近原始工厂），后注册者在外层（最终解析时最先执行）。

### 10.6 生命周期继承

装饰后的 descriptor 保持原始 `lifetime` 不变。

### 10.7 与其他机制的交互

- **replace + decorate**：`replace` 清除旧注册后添加新注册；`build()` 时已注册的装饰器会应用到替换后的新注册上
- **targeted decorator + replace**：若针对 `OldImpl` 的装饰器，但 `replace` 将该槽位换成 `NewImpl`（`impl_type` 不同），则该装饰器因不匹配而静默跳过
- **decorate + forward**：`forward` 展开后的 descriptor 是普通接口注册，全局装饰器会逐一应用于每条展开结果

---

## 11. 构建期校验

### 11.1 校验开关

`build()` 接受可选的配置结构，包含：

- `validate_on_build`（默认 `true`）：总开关，`false` 时跳过所有校验
- `validate_scopes`（默认 `true`）：控制是否执行生命周期兼容性检查

### 11.2 校验执行顺序

以下校验依次执行，任一失败即抛异常并停止后续校验：

1. 缺失依赖检查
2. 歧义依赖检查
3. 循环依赖检查
4. 生命周期兼容性检查（仅 `validate_scopes == true` 时）

### 11.3 缺失依赖检查

对所有 descriptor（包括 keyed）的 `dependencies` 中的每个类型，检查是否存在**至少一条 non-keyed** 注册。若不存在，抛 `not_found(dep_type)`。

背景：`deps<>` 在工厂执行时对每个依赖调用 non-keyed 的严格解析；keyed 依赖的注入需用户在自定义工厂中手动处理，不参与此检查。

### 11.4 歧义依赖检查

对所有 descriptor 的 `dependencies` 中每个类型，若该类型的 non-keyed 注册数量 > 1，抛 `ambiguous_component(dep_type)`。

**例外**：`forward` 占位符 descriptor 对其 `forward_target` 类型的依赖豁免此检查（forward 展开时用宽松解析，不用严格解析）。

### 11.5 循环依赖检查

对依赖图执行 DFS 三色标记（White / Gray / Black）的有向图环检测：

- **节点**：所有存在 non-keyed 注册的接口类型
- **边**：`dependencies` 中每条类型依赖关系
- **环的判定**：若发现当前正在访问节点（Gray）是某条边的目标（即存在反向边），则存在环

发现环时，抛 `cyclic_dependency(cycle_path)`，其中 `cycle_path` 包含环路中所有节点的 `type_index` 序列。

### 11.6 生命周期兼容性检查

对所有 descriptor 的 `dependencies` 中每个类型，取该类型**最后一条 non-keyed** 注册的 lifetime 作为"依赖的 lifetime"：

| 消费者 lifetime | 违规条件 | 抛出 |
|-----------------|----------|------|
| `singleton` | `dep.lifetime != singleton` | `lifetime_mismatch` |
| `scoped` | `dep.lifetime == transient` | `lifetime_mismatch` |
| `transient` | — | — |

`lifetime_mismatch` 必须携带：消费者类型、消费者 lifetime 名称、依赖类型、依赖 lifetime 名称。

---

## 12. 异常体系

### 12.1 继承层次

```
std::runtime_error
  └─ di_error                     ← 所有 DI 异常的基类
       ├─ not_found               ← 未找到注册
       ├─ cyclic_dependency       ← 循环依赖
       ├─ lifetime_mismatch       ← 生命周期违规
       ├─ no_active_scope         ← 在根 resolver 解析 scoped 组件
       ├─ duplicate_registration  ← 违反 single 策略
       ├─ resolution_error        ← 工厂执行时抛出异常的包装
       └─ ambiguous_component     ← 多注册歧义
```

### 12.2 各异常必须携带的信息

| 异常 | 必须携带 |
|------|----------|
| `di_error`（基类） | 消息字符串；`std::source_location`（抛出点） |
| `not_found` | `type_index`（未找到的类型）；可选 key 字符串 |
| `cyclic_dependency` | `vector<type_index>`（环路节点序列） |
| `lifetime_mismatch` | 消费者 `type_index` + lifetime 名；依赖 `type_index` + lifetime 名 |
| `no_active_scope` | `type_index`（被请求的 scoped 类型） |
| `duplicate_registration` | `type_index`；可选 key 字符串 |
| `resolution_error` | `type_index`；内层异常的 `what()` |
| `ambiguous_component` | `type_index`；可选 key 字符串 |

### 12.3 错误消息可读性要求

- 所有涉及类型的错误消息均应包含 demangled 类型名（`abi::__cxa_demangle` 或等效机制）
- 所有 `di_error` 子类均应在消息中附加调用处的源码位置（文件名、行号）

---

## 13. 线程安全

### 13.1 注册阶段

`registry` 的操作假定发生在单线程阶段（应用启动时），不要求线程安全。

### 13.2 解析阶段

`resolver` 在 `build()` 后可在多线程环境中并发使用，要求：

- **Singleton**：多线程首次解析时，工厂必须恰好被调用一次（once-per-type 语义）
- **Scoped**：同一 scope 内多线程并发解析时，工厂必须恰好被调用一次（once-per-scope-per-type 语义）
- **Transient**：每次都创建新实例，天然线程安全

### 13.3 锁的实现建议

每个缓存槽位（singleton 槽、scoped 槽）使用独立的互斥量保护。建议使用 `recursive_mutex`，以允许工厂闭包内部递归解析同类型（虽然这通常是配置错误，但不应导致死锁）。

---

## 14. 明确的非目标

以下功能**不在本 MVP 范围内**，实现时不应作为约束：

| 非目标 | 说明 |
|--------|------|
| 属性注入 | 仅支持构造函数注入 |
| 嵌套 scope | scoped resolver 不提供 `create_scope()` |
| Keyed forward | `forward<I,T>()` 仅展开 T 的 non-keyed 注册 |
| Keyed deps<> | `deps<>` 中的依赖始终通过 non-keyed 解析满足；keyed 注入需用户在自定义工厂中手动解析 |
| 预热/预创建 | `build()` 不预实例化任何 singleton |
| 命名 scope / child containers | 不提供类 ASP.NET request scope 语义 |
| 反射/代码生成 | 依赖必须在源码中明确以 `deps<>` 声明 |
| 跨库/插件注册合并 | 不提供多 registry 聚合机制 |
| Scoped 析构顺序保证 | 析构顺序取决于内部存储顺序，不保证逆拓扑序 |
| 自定义分配器 | 所有实例通过标准 `make_shared` 分配 |
| Windows / MSVC 支持 | 异常消息 demangling 依赖 `abi::__cxa_demangle`，仅限 GCC/Clang |

---

## 附录 A：关键决策记录

### A.1 类型擦除机制：`shared_ptr<void>` 而非虚基类

若要求所有可注册类型继承某个框架基类，会对用户代码产生强侵入。`shared_ptr<void>` 完全无侵入，`static_pointer_cast` 在已知具体类型时无运行时开销。同一套机制同时支持注册第三方类型。

### A.2 工厂类型别名命名：`factory_fn` 而非 `factory`

在 `descriptor` 结构内，若类型别名与成员均命名为 `factory`，编译器会报"declaration changes meaning of 'factory'"错误（[basic.scope.class]）。采用 `factory_fn` 作为类型别名、`factory` 作为成员名，可消除歧义，同时保持成员名的语义直观性。

### A.3 `single` 策略的"仅加锁"语义

常见场景是框架预注册默认实现，上层用 `single` 声明"此接口只允许一个实现"。若 `single` 在已有唯一注册时也抛异常，则框架预注册与上层锁定无法共存。`single` 的"仅加锁"行为（槽位唯一时不新增注册，仅设标记）解决了这个问题。

### A.4 `scoped → transient` 被列为生命周期违规

scoped 组件在整个 scope 内缓存同一实例，其注入的 transient 依赖实际上随之存活整个 scope，偏离了 transient "每次解析新建"的语义承诺。在大型系统中这会引入难以发现的共享状态 bug，因此明确将其列为违规。

### A.5 `forward` 展开在 `build()` 而非注册时执行

`forward<IBar, Foo>()` 注册时，`Foo` 的后续注册数量尚不确定。在 `build()` 时展开可以看到完整的 descriptor 集合，从而生成正确数量的 1:1 转发关系，并确保 lifetime 复制的一致性。

---

*本文档描述 MVP 需求；行为细节以 `tests/` 目录下的测试断言为规范补充来源。*
