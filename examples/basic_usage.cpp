/// basic_usage.cpp — librtdi introductory example.
///
/// Demonstrates the core registration → build → resolve workflow:
///   1. Define interfaces and implementations (no framework base classes).
///   2. Register with lifetime, dependencies declared via deps<>.
///   3. Call build() to validate the dependency graph.
///   4. Resolve services by interface; use scopes for scoped components.

#include <librtdi.hpp>
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using namespace librtdi;

// -----------------------------------------------------------------------
// Domain interfaces
// -----------------------------------------------------------------------

struct i_logger {
    virtual ~i_logger() = default;
    virtual void log(const std::string& message) = 0;
};

struct i_greeter {
    virtual ~i_greeter() = default;
    virtual std::string greet(const std::string& name) = 0;
};

struct i_request_context {
    virtual ~i_request_context() = default;
    virtual std::string request_id() const = 0;
};

// -----------------------------------------------------------------------
// Implementations
// -----------------------------------------------------------------------

struct console_logger : i_logger {
    void log(const std::string& message) override {
        std::cout << "[LOG] " << message << '\n';
    }
};

struct greeter : i_greeter {
    explicit greeter(std::shared_ptr<i_logger> logger)
        : logger_(std::move(logger)) {}

    std::string greet(const std::string& name) override {
        const auto msg = "Hello, " + name + '!';
        logger_->log(msg);
        return msg;
    }

private:
    std::shared_ptr<i_logger> logger_;
};

struct request_context : i_request_context {
    inline static int counter = 0;
    int id_;

    request_context() : id_(++counter) {}

    std::string request_id() const override {
        return "req-" + std::to_string(id_);
    }
};

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------

int main() {
    // ── Registration phase ────────────────────────────────────────────
    registry reg;

    // console_logger: singleton — one instance for the whole application.
    reg.add_singleton<i_logger, console_logger>();

    // greeter: singleton, depends on i_logger.
    reg.add_singleton<i_greeter, greeter>(deps<i_logger>);

    // request_context: scoped — one instance per scope (e.g. per HTTP request).
    reg.add_scoped<i_request_context, request_context>();

    // ── Build phase (validates dependency graph) ──────────────────────
    auto resolver = reg.build();

    // ── Resolution phase ──────────────────────────────────────────────

    // Singletons are resolved from the root resolver.
    const auto g1 = resolver->resolve<i_greeter>();
    const auto g2 = resolver->resolve<i_greeter>();
    assert(g1.get() == g2.get() && "singleton must return the same instance");
    std::cout << g1->greet("World") << '\n';

    // Scoped components require a scope.
    {
        auto scope1 = resolver->create_scope();
        auto scope2 = resolver->create_scope();

        auto& r1 = scope1->get_resolver();
        auto& r2 = scope2->get_resolver();

        const auto ctx1a = r1.resolve<i_request_context>();
        const auto ctx1b = r1.resolve<i_request_context>();
        const auto ctx2  = r2.resolve<i_request_context>();

        // Same scope → same instance.
        assert(ctx1a.get() == ctx1b.get());
        // Different scopes → different instances.
        assert(ctx1a.get() != ctx2.get());

        std::cout << "Scope 1 request id: " << ctx1a->request_id() << '\n';
        std::cout << "Scope 2 request id: " << ctx2->request_id() << '\n';
    }
    // Scopes released here; all scoped instances destroyed.

    std::cout << "Done.\n";
    return 0;
}
