#ifndef CDCL_RESTART_HPP_
#define CDCL_RESTART_HPP_

#include <string_view>

class restart_policy {
public:
    virtual ~restart_policy() = default;

    virtual void start() = 0;
    virtual void on_conflict() = 0;
    virtual bool should_restart() const = 0;
    virtual void on_restart() = 0;

    virtual std::string_view name() const = 0;
};

// Never restart.
class no_restart : public restart_policy {
public:
    void start() override {}
    void on_conflict() override {}
    bool should_restart() const override { return false; }
    void on_restart() override {}
    std::string_view name() const override { return "none"; }
};

// luby restarts: interval = base * luby(step)
class luby_restart : public restart_policy {
    double base_;
    unsigned step_ = 0;
    long long budget_ = 0;

    static double luby(unsigned i) {
        unsigned seq = i + 1;
        for (unsigned k = 1; k < 32; ++k) { // is i == 2^k - 1
            if (seq == (1u << k) - 1) return static_cast<double>(1u << (k - 1));
        }
        unsigned k = 1;
        // 2^(k-1) <= i < 2^k - 1
        while ((1u << k) - 1 < seq) ++k;
        // i-2^(k-1)+1
        return luby(seq - (1u << (k - 1)));
    }

public:
    explicit luby_restart(double base = 100.0) : base_(base) {}

    void start() override {
        step_ = 0;
        budget_ = static_cast<long long>(luby(step_) * base_);
    }
    void on_conflict() override { --budget_; }
    bool should_restart() const override { return budget_ <= 0; }
    void on_restart() override {
        ++step_;
        budget_ = static_cast<long long>(luby(step_) * base_);
    }
    std::string_view name() const override { return "luby"; }
};

// basic geometric restart
class geometric_restart : public restart_policy {
    double base_;
    double factor_;
    double limit_ = 0.0;
    long long budget_ = 0;

public:
    explicit geometric_restart(double base = 100.0, double factor = 1.5)
        : base_(base), factor_(factor) {}

    void start() override {
        limit_ = base_;
        budget_ = static_cast<long long>(limit_);
    }
    void on_conflict() override { --budget_; }
    bool should_restart() const override { return budget_ <= 0; }
    void on_restart() override {
        limit_ *= factor_;
        budget_ = static_cast<long long>(limit_);
    }
    std::string_view name() const override { return "geometric"; }
};

#endif
