# librtdi -- C++20 Runtime Dependency Injection Framework

`librtdi` is a C++20 DI/IoC framework inspired by .NET `Microsoft.Extensions.DependencyInjection` and Google `fruit`, featuring:

- Runtime type erasure (`erased_ptr` + `type_index`)
- Zero-macro dependency declaration (`deps<>`)
- Lifetime management (`singleton` / `transient`)
- Four-slot model (single-instance + collection x singleton + transient)
- Multi-implementation, keyed registration, forward, decorator
- `build()`-time validation (missing deps, cycles, lifetime violations)
- Multiple and virtual inheritance support

## Quick Start

### Minimal Example

```cpp
#include <librtdi.hpp>
#include <iostream>

struct IGreeter {
    virtual ~IGreeter() = default;
    virtual void greet() const = 0;
};

struct ConsoleGreeter : IGreeter {
    void greet() const override { std::cout << "Hello, DI!\n"; }
};

int main() {
    using namespace librtdi;

    registry reg;
    reg.add_singleton<IGreeter, ConsoleGreeter>();

    auto r = reg.build();
    r->get<IGreeter>().greet();  // returns IGreeter&
}
```

### Registration with Dependencies (`deps<>`)

```cpp
struct ILogger {
    virtual ~ILogger() = default;
    virtual void log(std::string_view msg) const = 0;
};

struct ConsoleLogger : ILogger {
    void log(std::string_view msg) const override {
        std::cout << "[LOG] " << msg << "\n";
    }
};

struct IService {
    virtual ~IService() = default;
    virtual void run() = 0;
};

// Dependencies are injected via constructor reference
struct MyService : IService {
    ILogger& logger_;
    explicit MyService(ILogger& logger) : logger_(logger) {}
    void run() override { logger_.log("running"); }
};

int main() {
    using namespace librtdi;
    registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_singleton<IService, MyService>(deps<ILogger>);
    //                                     ^^^^^^^^^^^
    //  deps<ILogger> -> injects ILogger& (singleton reference)

    auto r = reg.build();
    r->get<IService>().run();
}
```

## Core Concepts

### Lifetimes

| Enum | Meaning | Resolution API |
|------|---------|----------------|
| `singleton` | Global unique, created eagerly during `build()` by default | `get<T>()` returns `T&` |
| `transient` | New instance on every resolve | `create<T>()` returns `unique_ptr<T>` |

### Four-Slot Model

Each `(type, key)` pair can have up to 4 independent slots:

| Slot | Registration Method | Injection Type |
|------|---------------------|----------------|
| Singleton single | `add_singleton<I,T>()` | `T&` |
| Transient single | `add_transient<I,T>()` | `unique_ptr<T>` |
| Singleton collection | `add_collection<I,T>(lifetime_kind::singleton)` | `vector<T*>` |
| Transient collection | `add_collection<I,T>(lifetime_kind::transient)` | `vector<unique_ptr<T>>` |

### Dependency Tags

Each type parameter in `deps<>` determines the injection style:

| Tag | Injection Type | Resolver Method |
|-----|----------------|-----------------|
| `T` / `singleton<T>` | `T&` | `get<T>()` |
| `transient<T>` | `unique_ptr<T>` | `create<T>()` |
| `collection<T>` | `vector<T*>` | `get_all<T>()` |
| `collection<transient<T>>` | `vector<unique_ptr<T>>` | `create_all<T>()` |

## Registration API

### Single-Instance Registration

```cpp
using namespace librtdi;
registry reg;

// Zero-dep singleton
reg.add_singleton<IFoo, FooImpl>();

// Singleton with dependencies
reg.add_singleton<IBar, BarImpl>(deps<IFoo, transient<IBaz>>);

// Zero-dep transient
reg.add_transient<IFoo, FooImpl>();

// Transient with dependencies
reg.add_transient<IBar, BarImpl>(deps<IFoo>);
```

### Collection Registration

```cpp
// Multiple implementations registered to the same interface (freely appendable)
reg.add_collection<IPlugin, PluginA>(lifetime_kind::singleton);
reg.add_collection<IPlugin, PluginB>(lifetime_kind::singleton);

// Consumer receives all implementations via collection<IPlugin>
reg.add_singleton<PluginManager, PluginManager>(deps<collection<IPlugin>>);
```

### Keyed Registration

```cpp
reg.add_singleton<ICache, RedisCache>("redis");
reg.add_singleton<ICache, MemCache>("memory");

auto r = reg.build({.validate_on_build = false});
auto& redis = r->get<ICache>("redis");
auto& mem   = r->get<ICache>("memory");
```

### Forward Registration

Expose one implementation through multiple interfaces; singletons share the same instance:

```cpp
struct Impl : IA, IB {
    Impl() = default;
};

reg.add_singleton<Impl, Impl>();
reg.forward<IA, Impl>();  // IA singleton shares Impl's instance
reg.forward<IB, Impl>();  // IB likewise

auto r = reg.build();
auto& a = r->get<IA>();   // same underlying object as get<Impl>()
auto& b = r->get<IB>();   // same underlying object as get<Impl>()
```

### Decorator

Transparently wrap registered implementations with additional logic:

```cpp
struct LoggingFoo : IFoo {
    librtdi::decorated_ptr<IFoo> inner_;
    explicit LoggingFoo(librtdi::decorated_ptr<IFoo> inner)
        : inner_(std::move(inner)) {}

    void do_something() override {
        std::cout << "before\n";
        inner_->do_something();
        std::cout << "after\n";
    }
};

reg.add_singleton<IFoo, FooImpl>();
reg.decorate<IFoo, LoggingFoo>();

// Type-safe targeted decoration (compile-time check that TTarget derives from I)
reg.decorate_target<IFoo, LoggingFoo, FooImpl>();
```

Multiple decorators stack in registration order: first registered is innermost, last is outermost.

## Resolution API

```cpp
auto r = reg.build();

// Singleton (reference, lifetime bound to resolver)
auto& svc = r->get<IFoo>();           // IFoo&, throws not_found if unregistered
auto* ptr = r->try_get<IFoo>();       // IFoo* or nullptr

// Transient (ownership transferred to caller)
auto obj = r->create<IBar>();         // unique_ptr<IBar>, throws not_found if unregistered
auto opt = r->try_create<IBar>();     // unique_ptr<IBar> or empty

// Collections
auto all  = r->get_all<IPlugin>();    // vector<IPlugin*> (singleton collection)
auto allT = r->create_all<IPlugin>(); // vector<unique_ptr<IPlugin>> (transient collection)

// Keyed variants
auto& redis = r->get<ICache>("redis");
auto conn   = r->create<IConnection>("primary");
```

## Build-Time Validation

`build()` automatically validates before creating the resolver:

```cpp
auto r = reg.build();  // all validations enabled by default
```

Optional controls:

```cpp
auto r = reg.build({
    .validate_on_build  = true,   // master switch
    .validate_lifetimes = true,   // check captive dependency
    .detect_cycles      = true,   // check circular dependencies
    .eager_singletons   = true,   // instantiate all singletons during build()
});
```

When `eager_singletons` is `true` (default), all singleton factories are invoked during `build()`, so factory exceptions surface immediately and first-request latency is eliminated. Set to `false` for lazy initialization.

Validation order: missing dependencies, then lifetime compatibility, then cycle detection, then eager singleton instantiation.

## Inheritance Model

librtdi supports all C++ inheritance forms:

| Inheritance | Supported | Notes |
|-------------|-----------|-------|
| Single | Yes | Standard scenario |
| Multiple | Yes | Pointer offset handled automatically via `make_erased_as<TInterface, TImpl>()` |
| Virtual | Yes | Upcast `static_cast` (derived to base) is legal for virtual inheritance |
| Diamond | Yes | Used in combination with virtual inheritance |

**Requirement**: When `TInterface != TImpl`, `TInterface` must have a virtual destructor (enforced by compile-time `static_assert`).

## Exception Hierarchy

```
std::runtime_error
  +-- di_error                     <-- base for all DI exceptions
       +-- not_found               <-- no registration found
       +-- cyclic_dependency       <-- circular dependency detected
       +-- lifetime_mismatch       <-- lifetime violation (captive dependency)
       +-- duplicate_registration  <-- duplicate single-instance slot registration
       +-- resolution_error        <-- wraps factory exceptions
```

All exception messages include demangled type names and source location (pointing to the user's call site, not library internals). Key diagnostic features:

- **`source_location` accuracy**: All public template methods capture `std::source_location::current()` at the user call site, ensuring exception locations are meaningful
- **Registration location tracking**: `descriptor` stores where each component was registered; `resolution_error` and `not_found` messages include "(registered at file:line)"
- **Consumer info in `not_found`**: Validation-phase `not_found` messages include the consumer type, impl type, lifetime, and registration location
- **Impl info in `lifetime_mismatch`**: Messages optionally include the concrete implementation type name
- **Non-standard exception pass-through**: Factory exceptions that don't derive from `std::exception` are not caught — they propagate to the caller as-is
- **Contextual `static_assert`**: Each compile-time assertion names the specific API (e.g., `"add_singleton<I,T>: I must have a virtual destructor when I != T"`)
- **Slot hints in `not_found`**: When the type exists in a different slot (e.g., registered as transient but requested via `get<T>()`), the message suggests the correct method

## Thread Safety

- **Registration phase**: `registry` assumes single-threaded use
- **Resolution phase**: `resolver` is safe for concurrent multi-threaded use; singleton creation is protected by a `recursive_mutex` ensuring once-per-descriptor semantics

## Building

librtdi builds as a **shared library** (`librtdi.so` / `librtdi.dylib` / `rtdi.dll`) by default.

```bash
cmake -B build -G Ninja
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Package
cmake --build build --target package
```

### Cross-Platform Notes

| Platform | Library Output | Notes |
|----------|----------------|-------|
| Linux | `librtdi.so.0.1.1` (SOVERSION symlinks) | `-fvisibility=hidden`; only `LIBRTDI_EXPORT` symbols exported |
| macOS | `librtdi.0.1.1.dylib` | `MACOSX_RPATH ON`; `@loader_path` RPATH |
| Windows | `rtdi.dll` + `rtdi.lib` (import lib) | `__declspec(dllexport/dllimport)` via `LIBRTDI_EXPORT` macro |

To build and link as a **static library**, define `LIBRTDI_STATIC` before including any librtdi header, and change `SHARED` to `STATIC` in `src/CMakeLists.txt`.

### Downstream Integration

After installation, integrate via standard CMake `find_package`:

```cmake
find_package(librtdi CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE librtdi::librtdi)
```

## Full Example

See [examples/basic_usage.cpp](examples/basic_usage.cpp) for a complete demo of singleton, deps injection, collections, and decorator composition.

## Project Structure

```text
librtdi/
├── CMakeLists.txt
├── README.md
├── REQUIREMENTS.md
├── cmake/
│   ├── CompilerWarnings.cmake
│   ├── Dependencies.cmake
│   └── librtdiConfig.cmake.in
├── include/
│   ├── librtdi.hpp
│   └── librtdi/
│       ├── export.hpp
│       ├── fwd.hpp
│       ├── lifetime.hpp
│       ├── erased_ptr.hpp
│       ├── decorated_ptr.hpp
│       ├── descriptor.hpp
│       ├── exceptions.hpp
│       ├── registry.hpp
│       ├── resolver.hpp
│       └── type_traits.hpp
├── src/
│   ├── exceptions.cpp
│   ├── registry.cpp
│   ├── resolver.cpp
│   └── validation.cpp
├── tests/
│   ├── test_auto_wiring.cpp
│   ├── test_concurrency.cpp
│   ├── test_decorator.cpp
│   ├── test_diagnostics.cpp
│   ├── test_eager.cpp
│   ├── test_edge_cases.cpp
│   ├── test_forward.cpp
│   ├── test_inheritance.cpp
│   ├── test_keyed.cpp
│   ├── test_lifetime.cpp
│   ├── test_multi_impl.cpp
│   ├── test_registration.cpp
│   ├── test_resolution.cpp
│   └── test_validation.cpp
└── examples/
    └── basic_usage.cpp
```
