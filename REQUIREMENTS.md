# 需求规格说明书

**项目名称**：librtdi — C++20 运行时依赖注入框架（MVP）  
**文档版本**：2.2  
**语言**：C++20  
**最后更新**：2026-02-24

---

## 目录

1. [背景与目标](#1-背景与目标)
2. [核心概念](#2-核心概念)
3. [组件描述符](#3-组件描述符)
4. [注册 API](#4-注册-api)
5. [类型擦除存储：erased_ptr](#5-类型擦除存储erased_ptr)
6. [生命周期语义](#6-生命周期语义)
7. [解析 API](#7-解析-api)
8. [转发注册（Forward）](#8-转发注册forward)
9. [装饰器（Decorator）](#9-装饰器decorator)
10. [构建期校验](#10-构建期校验)
11. [异常体系](#11-异常体系)
12. [线程安全](#12-线程安全)
13. [明确的非目标](#13-明确的非目标)

---

## 1. 背景与目标

### 1.1 动机

提供一个适用于 C++20 的**运行时**依赖注入框架，满足以下需求：

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
| 轻量级存储 | 使用 `erased_ptr`（原始函数指针 deleter）代替 `shared_ptr<void>` |
| 继承模型支持 | 支持单继承、多重继承与虚拟继承（virtual base），所有指针调整在注册时通过上行 `static_cast` 完成 |

---

## 2. 核心概念

### 2.1 框架整体流程

框架分为**三个阶段**，各阶段有明确边界：

```
注册阶段 (registry)
  └─ add_singleton / add_transient / add_collection / forward / decorate
        ↓
构建阶段 (registry::build)
  └─ 展开 forward → 应用 decorator → 校验依赖图 → 生成 resolver
        ↓
解析阶段 (resolver)
  └─ get / create / get_all / create_all
```

`registry::build()` 调用后，registry 本身的 descriptor 集合被移交给 resolver，registry 不再持有有效状态。`build()` 返回的 `resolver`（`shared_ptr<resolver>`）独立于 `registry` 存活；`registry` 可在 `build()` 后安全销毁。

`build()` 仅允许调用一次；第二次调用将抛出 `di_error`。`build()` 调用后，再调用任何注册方法（`add_singleton` / `add_transient` / `add_collection` / `forward` / `decorate`）同样抛出 `di_error`。

### 2.2 接口与实现的关系

- 每一条注册均绑定一个**接口类型**（用于查找）和一个**具体实现类型**（用于构造）
- 具体实现类型必须派生自接口类型（编译期 `is_base_of` 断言）
- 当 `TInterface != TImpl` 时，`TInterface` **必须拥有虚析构函数**（编译期 `static_assert`），以保证通过基类指针正确删除派生对象
- 当 `TInterface == TImpl` 时，注册的是具体类型自身，不要求虚析构函数

### 2.3 四槽位模型 (4-Slot Model)

每个 `(component_type, key)` 对可以拥有最多 **4 个独立槽位**，由 `(type_index, string, lifetime_kind, bool is_collection)` 四元组唯一标识：

| 槽位 | lifetime | is_collection | 注册方法 | 注入类型 |
|------|----------|---------------|----------|----------|
| 单例单实例 | singleton | false | `add_singleton` | `T&` |
| 瞬态单实例 | transient | false | `add_transient` | `std::unique_ptr<T>` |
| 单例集合 | singleton | true | `add_collection(lifetime_kind::singleton)` | `std::vector<T*>` |
| 瞬态集合 | transient | true | `add_collection(lifetime_kind::transient)` | `std::vector<std::unique_ptr<T>>` |

- **单实例槽位**：同一 `(type, key, lifetime)` 下最多允许 1 条注册，重复注册抛 `duplicate_registration`
- **集合槽位**：同一 `(type, key, lifetime)` 下允许任意多条注册（自由追加）

### 2.4 依赖的表达方式

依赖通过一个零大小的**标签类型** `deps<D1, D2, ...>` 在注册点声明，传入注册方法作为参数。每个 `Di` 可以是以下 5 种形式之一：

| 依赖标记 | 含义 | 注入类型 |
|----------|------|----------|
| `T` (bare) | 单例依赖 | `T&` |
| `singleton<T>` | 显式单例标记（等同于 bare T） | `T&` |
| `transient<T>` | 瞬态依赖 | `std::unique_ptr<T>` |
| `collection<T>` | 单例集合依赖 | `std::vector<T*>` |
| `collection<singleton<T>>` | 等同于 `collection<T>` | `std::vector<T*>` |
| `collection<transient<T>>` | 瞬态集合依赖 | `std::vector<std::unique_ptr<T>>` |

示例：

```cpp
registry.add_singleton<IFoo, FooImpl>(deps<IBar, transient<IBaz>, collection<IPlugin>>);
```

`deps<>` 要求：
- `TImpl` 必须可由注入类型列表构造（编译期断言：`constructible_from_deps` concept）
- 解析时框架对每个 `Di` 按其标记执行对应的解析方法

无依赖时直接调用不带标签的重载：

```cpp
registry.add_singleton<IFoo, FooImpl>();
// TImpl 必须默认可构造
```

---

## 3. 组件描述符

描述符（`descriptor`）是注册信息的运行时表示，包含以下语义字段：

| 字段 | 类型 | 语义 |
|------|------|------|
| `component_type` | `type_index` | 接口类型，查找 key 的一部分 |
| `lifetime` | `lifetime_kind` | 生命周期枚举（singleton / transient） |
| `factory` | `factory_fn` | 工厂函数，`std::function<erased_ptr(resolver&)>` |
| `dependencies` | `vector<dependency_info>` | 依赖元数据列表（用于 build-time 校验） |
| `key` | `string` | 空字符串表示非命名注册；非空为 keyed 注册 |
| `is_collection` | `bool` | 该注册是否属于集合槽位 |
| `impl_type` | `optional<type_index>` | 具体实现类型（用于装饰器精确匹配） |
| `forward_target` | `optional<type_index>` | 非空时表示这是一条 forward 展开后的记录 |
| `forward_cast` | `function<void*(void*)>` | forward 展开时的指针偏移转换 |

### 3.1 dependency_info

依赖元数据结构：

| 字段 | 类型 | 语义 |
|------|------|------|
| `type` | `type_index` | 被依赖的接口类型 |
| `is_collection` | `bool` | 是否为集合依赖 |
| `is_transient` | `bool` | 是否为瞬态依赖 |

校验时用 `(type, is_collection, is_transient)` 三元组确定所需槽位。

---

## 4. 注册 API

### 4.1 注册方法矩阵

registry 提供以下注册方法，所有方法均返回 `registry&`（支持链式调用）：

**单实例注册：**

| 方法签名 | 生命周期 | 依赖来源 |
|----------|----------|----------|
| `add_singleton<I,T>()` | singleton | 无（T 默认可构造） |
| `add_singleton<I,T>(deps<D...>)` | singleton | deps<> 标签 |
| `add_transient<I,T>()` | transient | 无 |
| `add_transient<I,T>(deps<D...>)` | transient | deps<> 标签 |

**集合注册：**

| 方法签名 | 依赖来源 |
|----------|----------|
| `add_collection<I,T>(lifetime_kind)` | 无 |
| `add_collection<I,T>(lifetime_kind, deps<D...>)` | deps<> 标签 |

**命名注册（keyed）：**

以上所有方法均存在对应的 keyed 重载，通过在参数列表首位增加 `string_view key` 区分。keyed 与 non-keyed 注册在同一接口类型下互不干扰。

### 4.2 编译期约束

每个 `add_*<I, T>` 调用均在编译期检查：

- `T` 派生自 `I`（`derived_from_base<T, I>` concept）
- 无依赖版本：`T` 默认可构造（`default_constructible<T>` concept）
- 带依赖版本：`T` 可由注入类型列表构造（`constructible_from_deps<T, Deps...>` concept）

### 4.3 工厂函数语义

每条注册在内部持有一个工厂函数，签名为 `erased_ptr(resolver&)`：

- 工厂在解析时被调用，接收当前激活的 `resolver`
- 工厂返回类型擦除的 `erased_ptr`；框架在类型安全边界处通过 `static_cast<T*>` 恢复类型
- 工厂执行时若抛出异常：若异常属于 `di_error` 子类（如 `not_found`），框架直接透传不做二次包装；其他 `std::exception` 子类则包装为 `resolution_error` 再向上传播

### 4.4 单实例槽位唯一性

对同一 `(component_type, key, lifetime)` 的单实例槽位，第二次调用 `add_singleton` 或 `add_transient` 将抛 `duplicate_registration`。不同 lifetime 的单实例槽位相互独立，同一接口可以同时拥有 singleton 和 transient 注册。

---

## 5. 类型擦除存储：erased_ptr

### 5.1 设计动机

标准 `unique_ptr<void>` 在默认删除器下不可用（`void` 是不完整类型，`default_delete<void>` 无法调用 `delete`），而 `shared_ptr<void>` 虽然可用但引入了引用计数开销。`erased_ptr` 是一个最小化的 RAII 包装器，等价于 `unique_ptr<void, void(*)(void*)>`。

### 5.2 定义

```cpp
struct erased_ptr {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;

    erased_ptr() = default;
    erased_ptr(void* p, void (*d)(void*)) noexcept;

    erased_ptr(erased_ptr&&) noexcept;
    erased_ptr& operator=(erased_ptr&&) noexcept;

    erased_ptr(const erased_ptr&) = delete;
    erased_ptr& operator=(const erased_ptr&) = delete;

    ~erased_ptr();

    void  reset() noexcept;
    void* get() const noexcept;
    void* release() noexcept;
    explicit operator bool() const noexcept;
};
```

### 5.3 关键特性

- 大小恰为 **2 个指针**（16字节 / 64位平台），无堆分配
- `deleter` 为原始函数指针 `void(*)(void*)`，不使用 `std::function`
- 移动语义（noexcept）、不可复制
- `make_erased<T>(args...)` 辅助函数：创建 `new T(args...)` 并设置正确的 deleter（仅用于 `TInterface == TImpl` 场景）
- `make_erased_as<TInterface, TImpl>(args...)` 辅助函数：创建 `new TImpl(args...)`，执行 `static_cast<TInterface*>(impl)` 后将 `TInterface*` 存入 `void*`。这确保 `static_cast<TInterface*>(void*)` 在解析时正确还原指针，即使在多重继承或虚拟继承下指针值发生偏移也不会出错。要求 `TInterface` 拥有虚析构函数（当 `TInterface != TImpl` 时）

### 5.4 非所有权 erased_ptr

当 `deleter == nullptr` 时，`reset()` 不执行删除。用于 forward-singleton 场景（指向目标 singleton 的缓存实例，由原始描述符拥有）。

---

## 6. 生命周期语义

### 6.1 两种生命周期

| 枚举值 | 含义 |
|--------|------|
| `lifetime_kind::singleton` | 全局唯一。在 resolver 上首次解析时创建；此后所有解析均返回同一实例 |
| `lifetime_kind::transient` | 每次解析均创建新实例；不缓存 |

### 6.2 缓存位置

- Singleton 实例由 `resolver` 内部的缓存（`erased_ptr`）持有，以 descriptor 索引为 key
- Transient 实例不缓存，每次调用工厂创建新实例

### 6.3 懒初始化

Singleton 实例在首次被请求时才调用工厂创建，而非在 `build()` 时预创建。

---

## 7. 解析 API

### 7.1 Resolver 是只读的

resolver 在 `build()` 后不再接受注册操作，其接口仅提供解析方法。

### 7.2 非命名解析方法

| 方法 | 返回类型 | 行为 |
|------|----------|------|
| `get<T>()` | `T&` | 返回 singleton 实例引用；未注册抛 `not_found` |
| `try_get<T>()` | `T*` | 返回 singleton 实例指针；未注册返回 `nullptr` |
| `create<T>()` | `unique_ptr<T>` | 创建新 transient 实例；未注册抛 `not_found` |
| `try_create<T>()` | `unique_ptr<T>` | 创建新 transient 实例；未注册返回空 |
| `get_all<T>()` | `vector<T*>` | 返回所有 singleton 集合项；无注册时返回空容器 |
| `create_all<T>()` | `vector<unique_ptr<T>>` | 创建所有 transient 集合项；无注册时返回空容器 |

> **引用生命周期**：`get<T>()` 和 `try_get<T>()` 返回的引用/指针的生命周期与 `resolver` 对象绑定。`resolver` 销毁后，所有通过这些方法获得的引用/指针均失效（dangling）。

### 7.3 命名解析方法

与非命名解析方法一一对应，增加 `string_view key` 参数。keyed 解析仅匹配相同 key 的注册；non-keyed 与 keyed 注册之间互不可见。

### 7.4 `deps<>` 在解析时的行为

`deps<Di...>` 中每个 `Di` 根据其标记类型调用对应的解析方法：

| 依赖标记 | 调用方法 |
|----------|----------|
| bare `T` / `singleton<T>` | `resolver.get<T>()` |
| `transient<T>` | `resolver.create<T>()` |
| `collection<T>` / `collection<singleton<T>>` | `resolver.get_all<T>()` |
| `collection<transient<T>>` | `resolver.create_all<T>()` |

---

## 8. 转发注册（Forward）

### 8.1 目的

允许一个具体实现 `T` 同时以多个接口类型被解析。对于 singleton 槽位，多个接口共享**同一实例**（forward 的 singleton 使用非所有权 erased_ptr 指向目标 singleton）。

### 8.2 注册时语义

`registry.forward<IBar, Foo>()` 要求 `Foo` 派生自 `IBar`（编译期断言）。注册一条前向记录，`build()` 时展开。

### 8.3 `build()` 阶段的展开规则

1. 遍历所有 forward 记录
2. 对每条记录，查找所有 `component_type == typeid(Foo)` 且 non-keyed 的真实 descriptor
3. 为每条目标 descriptor 生成一条新 descriptor：
   - `component_type = typeid(IBar)`
   - `lifetime` 复制自目标 descriptor
   - `is_collection` 复制自目标 descriptor
   - Singleton：工厂通过 `resolve_singleton_by_index` 解析目标实例，再通过 `forward_cast` 调整指针，返回非所有权 erased_ptr
   - Transient：工厂通过 `resolve_transient_by_index` 创建目标实例，调整指针后转移所有权
4. 将展开后的 descriptor 追加到集合中

### 8.4 forward 传播全部槽位

一次 `forward<IBar, Foo>()` 调用会将 Foo 的所有**非命名**已注册槽位（singleton 单实例、transient 单实例、singleton 集合、transient 集合）全部传播到 IBar。keyed 注册不受 forward 影响。

### 8.5 指针转换

`forward_cast` 函数执行 `void* → TTarget* → TInterface*` 的 static_cast 链。由于向上转型（derived → base）的 `static_cast` 在所有继承形式下均合法且正确（包括单继承、多重继承和虚拟继承），此机制适用于所有继承模型。

对于 forward-transient 场景，框架使用专用的 `forward_deleter`（在 `forward<I, T>()` 模板中生成），执行 `delete static_cast<TInterface*>(p)`，确保在多重继承指针偏移下也能正确释放对象。

### 8.6 当前限制

- `forward<I, T>()` 仅对 `T` 的 non-keyed 注册展开；keyed 形式不在本 MVP 范围内
- **链式 forward**（如 `forward<IC, IB>()` + `forward<IB, Foo>()`）不受支持；所有 forward 记录在 `build()` 时独立展开，后展开的 descriptor 对先执行的 forward 不可见
- 若 `forward<I, T>()` 在 `build()` 时找不到 T 的任何 non-keyed 注册，框架会在构建期校验阶段抛出 `not_found(T)`

---

## 9. 装饰器（Decorator）

### 9.1 目的

在不修改原始注册、不影响消费方的前提下，为一个或多个已注册实现透明地包裹额外逻辑（装饰器模式）。

### 9.2 装饰器注册时机

装饰器注册**不立即修改 descriptor 集合**，在内部维护一个待应用的 decorator 回调列表；`build()` 时统一应用。

### 9.3 四种注册形式

| 形式 | 匹配范围 | 构造约束 |
|------|----------|----------|
| `decorate<I, D>()` | `I` 的所有注册 | `D(unique_ptr<I>)` 可构造 |
| `decorate<I, D>(deps<Extra...>)` | 同上 | `D(unique_ptr<I>, inject_type_t<Extra>...)` 可构造 |
| `decorate<I, D>(type_index target)` | `impl_type == target` 的注册 | `D(unique_ptr<I>)` 可构造 |
| `decorate<I, D>(type_index target, deps<Extra...>)` | 同上 | `D(unique_ptr<I>, inject_type_t<Extra>...)` 可构造 |
| `decorate_target<I, D, TTarget>()` | `impl_type == typeid(TTarget)` | `D(unique_ptr<I>)` 可构造；编译期断言 `TTarget` 派生自 `I` |
| `decorate_target<I, D, TTarget>(deps<Extra...>)` | 同上 | `D(unique_ptr<I>, inject_type_t<Extra>...)` 可构造 |

`decorate_target` 是 `decorate(type_index)` 的类型安全替代形式，避免手动构造 `type_index(typeid(...))`，并在编译期验证 `TTarget` 与 `I` 的继承关系。

编译期要求：`D` 派生自 `I`；构造签名使用 `unique_ptr<I>`（非 `shared_ptr<I>`）。

### 9.4 `build()` 阶段的应用规则

按装饰器的注册顺序逐一处理：

- **全局装饰器**（无 target）：`d.component_type == typeid(I)` 时应用
- **目标装饰器**：`d.component_type == typeid(I)` 且 `d.impl_type == target` 时应用
- **Forward-singleton 除外**：装饰器**不应用于** forward 展开的 singleton 描述符。原因：forward-singleton 的工厂返回非所有权 `erased_ptr`（`deleter == nullptr`），而装饰器 wrapper 假设工厂返回拥有所有权的 `erased_ptr` 并通过 `release()` → `unique_ptr<I>` 接管所有权。若应用，resolver 销毁时会导致 double-free。Forward-transient 不受此限制（其工厂返回拥有所有权的 `erased_ptr`）
- **应用方式**：`d.factory = decorator_wrapper(d.factory)`

descriptor 的 `impl_type` 不会因装饰而改变（精确匹配仍基于原始实现类型）。

> **推荐做法**：若需装饰 forward 可见的 singleton 接口，应在原始注册（`T` 本身）上应用装饰器。由于 forward-singleton 共享同一底层实例，对原始工厂的装饰效果会自然通过 forward 视图可见。

### 9.5 链式叠加顺序

多个 decorator 按注册顺序叠加：先注册者在内层（更靠近原始工厂），后注册者在外层。

### 9.6 生命周期继承

装饰后的 descriptor 保持原始 `lifetime` 不变。

---

## 10. 构建期校验

### 10.1 校验开关

`build()` 接受可选的 `build_options` 结构：

| 字段 | 默认值 | 含义 |
|------|--------|------|
| `validate_on_build` | `true` | 总开关，`false` 时跳过所有校验 |
| `validate_lifetimes` | `true` | 控制是否执行生命周期兼容性检查 |
| `detect_cycles` | `true` | 控制是否执行循环依赖检测 |

### 10.2 校验执行顺序

以下校验依次执行，任一失败即抛异常并停止后续校验：

1. 缺失依赖检查
2. 生命周期兼容性检查（仅 `validate_lifetimes == true` 时）
3. 循环依赖检查（仅 `detect_cycles == true` 时）

### 10.3 缺失依赖检查

对所有 descriptor 的 `dependencies` 中的每条 `dependency_info`：

- 根据 `(dep.type, dep.is_transient ? transient : singleton, dep.is_collection)` 确定所需槽位
- 检查该槽位是否存在至少一条注册
- 若不存在，抛 `not_found(dep.type)`

### 10.4 生命周期兼容性检查（captive dependency 检测）

对所有 `lifetime == singleton` 的 descriptor 的 `dependencies`：

| 依赖类型 | 是否违规 | 说明 |
|----------|----------|------|
| `is_transient && !is_collection` | **违规** | 单例捕获了瞬态单实例 → 抛 `lifetime_mismatch` |
| `is_transient && is_collection` | 合法 | 单例在初始化时获取一批 transient 集合是可接受的模式 |
| `!is_transient` | 合法 | 依赖同为 singleton |

### 10.5 循环依赖检查

对依赖图执行 DFS 三色标记环检测：

- **节点**：所有接口类型（以 `type_index` 为粒度，忽略 lifetime 和 is_collection 差异）
- **边**：`dependency_info` 中的类型依赖关系
- **环的判定**：发现 Gray→Gray 的反向边

发现环时，抛 `cyclic_dependency(cycle_path)`，其中 `cycle_path` 包含环路中所有节点的 `type_index` 序列（序列末尾重复起始节点以闭合环路，例如 `[A, B, A]`）。

---

## 11. 异常体系

### 11.1 继承层次

```
std::runtime_error
  └─ di_error                     ← 所有 DI 异常的基类
       ├─ not_found               ← 未找到注册
       ├─ cyclic_dependency       ← 循环依赖
       ├─ lifetime_mismatch       ← 生命周期违规（captive dependency）
       ├─ duplicate_registration  ← 单实例槽位重复注册
       └─ resolution_error        ← 工厂执行时抛出异常的包装
```

### 11.2 各异常必须携带的信息

| 异常 | 必须携带 |
|------|----------|
| `di_error`（基类） | 消息字符串；`std::source_location`（抛出点） |
| `not_found` | `type_index`（未找到的类型）；可选 key 字符串 |
| `cyclic_dependency` | `vector<type_index>`（环路节点序列） |
| `lifetime_mismatch` | 消费者 `type_index` + lifetime 名；依赖 `type_index` + lifetime 名 |
| `duplicate_registration` | `type_index`；可选 key 字符串 |
| `resolution_error` | `type_index`；内层异常的 `what()` |

### 11.3 错误消息可读性要求

- 所有涉及类型的错误消息均应包含 demangled 类型名（`abi::__cxa_demangle`）
- 所有 `di_error` 子类均应在消息中附加调用处的源码位置（文件名、行号）
- **`not_found` 槽位提示**：当 `get<T>()` 或 `create<T>()` 因槽位不匹配而抛出 `not_found` 时（例如类型注册为 transient 而通过 `get<T>()` 解析），错误消息应包含诊断提示，指明该类型在哪个槽位存在以及应使用哪个方法（如 "type is registered as transient (use create<T>())"）

---

## 12. 线程安全

### 12.1 注册阶段

`registry` 的操作假定发生在单线程阶段（应用启动时），不要求线程安全。

### 12.2 解析阶段

`resolver` 在 `build()` 后可在多线程环境中并发使用，要求：

- **Singleton**：多线程首次解析时，同一 descriptor 的工厂必须恰好被调用一次（once-per-descriptor 语义）
- **Transient**：每次都创建新实例，天然线程安全

### 12.3 锁的实现

Singleton 缓存整体使用一把 `recursive_mutex` 保护，允许工厂闭包内部递归解析（虽然这通常是循环依赖配置错误，但不应导致死锁）。

---

## 13. 明确的非目标

以下功能**不在本 MVP 范围内**：

| 非目标 | 说明 |
|--------|------|
| 属性注入 | 仅支持构造函数注入 |
| 作用域 (Scope) | 不提供 scoped 生命周期，不提供嵌套容器 |
| 注册策略 (Policy) | 不提供 single / replace / skip 策略；单实例槽位强制唯一，集合槽位自由追加 |
| Keyed forward | `forward<I,T>()` 仅展开 T 的 non-keyed 注册 |
| Keyed deps<> | `deps<>` 中的依赖始终通过 non-keyed 解析满足 |
| 预热/预创建 | `build()` 不预实例化任何 singleton |
| 反射/代码生成 | 依赖必须在源码中明确以 `deps<>` 声明 |
| 跨库/插件注册合并 | 不提供多 registry 聚合机制 |
| 自定义分配器 | 所有实例通过标准 `new` 分配 |
| 无虚析构函数的接口基类 | 当 `TInterface != TImpl` 时，`TInterface` **必须**拥有虚析构函数；框架通过 `static_assert` 在编译期强制此约束 |
| MSVC 全面支持 | 异常消息 demangling 依赖 `abi::__cxa_demangle`；MSVC 下退化为编译器原生 `type_index::name()`，功能正常但可读性降低。MSVC 未经全面测试 |

---

## 附录 A：关键设计决策记录

### A.1 erased_ptr 而非 shared_ptr<void>

`shared_ptr<void>` 虽然可以类型擦除 deleter，但引入引用计数开销。MVP 中 singleton 由 resolver 独占管理（lifetime 绑定 resolver），无需共享所有权。`erased_ptr` 仅 2 个指针大小，移动操作 noexcept 且无堆分配。

### A.2 四槽位模型而非 registration_policy

旧设计使用 `multiple` / `single` / `replace` / `skip` 策略控制同一接口的重复注册。新设计将单实例与集合在类型层面分离：

- `add_singleton` / `add_transient`：单实例槽位，同 (type, key, lifetime) 只允许一个
- `add_collection`：集合槽位，自由追加

这消除了策略的复杂性，同时保留了"同一接口多实现"的能力。

### A.3 deps<> 依赖标记类型

旧设计中 `deps<Di...>` 每个 Di 均以 `shared_ptr<Di>` 注入。新设计引入标记类型以在编译期区分注入方式：

- Bare `T` / `singleton<T>` → `T&`（引用注入，由 resolver 持有实例）
- `transient<T>` → `unique_ptr<T>`（所有权转移）
- `collection<T>` → `vector<T*>`（指针集合）
- `collection<transient<T>>` → `vector<unique_ptr<T>>`（所有权集合）

### A.4 装饰器构造签名使用 unique_ptr

装饰器接收被装饰实例的所有权（`unique_ptr<I>`），而非共享引用（`shared_ptr<I>`）。这与 erased_ptr 的所有权语义一致：resolver 通过工厂创建 erased_ptr → release → 转换为 `unique_ptr<I>` → 传入装饰器构造函数。

### A.5 forward 展开在 build() 时执行

`forward<IBar, Foo>()` 注册时，Foo 的后续注册数量尚不确定。在 `build()` 时展开可以看到完整的 descriptor 集合，从而生成正确数量的转发关系，并确保 lifetime / is_collection 复制的一致性。

### A.6 多重继承与虚拟继承支持

旧设计中 `make_erased<TImpl>()` 将 `TImpl*` 直接存入 `void*`，解析时通过 `static_cast<TInterface*>(void*)` 恢复。在单继承下这是正确的（指针值不变），但在**多重继承**中，`TInterface` 不是第一个基类时指针偏移不为零，该 round-trip 会产生未定义行为。

新设计引入 `make_erased_as<TInterface, TImpl>()`：

1. `new TImpl(...)` 创建实例
2. `static_cast<TInterface*>(impl)` 执行编译期已知的上行转型，得到正确偏移的 `TInterface*`
3. 将 `TInterface*` 而非 `TImpl*` 存入 `void*`
4. deleter 执行 `delete static_cast<TInterface*>(p)`，要求 `TInterface` 拥有虚析构函数

这使得解析时的 `static_cast<TInterface*>(void*)` 成为恒等转换，在所有继承形式下均正确。上行 `static_cast`（derived → base）对虚拟继承也是合法的，因此虚拟继承自然获得支持，无需 `dynamic_cast`。

---

*本文档描述 MVP 需求；行为细节以 `tests/` 目录下的测试断言为规范补充来源。*
