#ifndef CDCL_DELETION_HPP_
#define CDCL_DELETION_HPP_

#include "tseitin/tseitin.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

struct reduce_context {
    const std::vector<clause_t>& clauses; // all clauses
    const std::vector<bool>& locked; // must be kept
};


class clause_deletion {
public:
    virtual ~clause_deletion() = default;

    virtual void start(std::size_t clause_count) = 0;
    /**
     * @brief 
     * 
     * @param c index of clause
     * @param clause learnt clause
     * @param level_of decision levels of variables
     */
    virtual void on_learn(std::size_t c, const clause_t& clause,
                          const std::vector<int>& level_of) = 0;
    virtual void on_used(std::size_t c) = 0;
    virtual void on_conflict() = 0;

    virtual bool full() const = 0;
    virtual std::size_t num_learnts() const = 0;

    virtual void select(const reduce_context& ctx, std::vector<std::uint8_t>& remove) const = 0;
    virtual void on_compact(const std::vector<std::uint8_t>& remove) = 0;
    virtual void on_reduced() = 0;

    virtual std::string_view name() const = 0;
};

// does not delete anything
class no_deletion : public clause_deletion {
    std::size_t num_learnts_ = 0;

public:
    void start(std::size_t) override { num_learnts_ = 0; }
    void on_learn(std::size_t, const clause_t&, const std::vector<int>&) override { ++num_learnts_; }
    void on_used(std::size_t) override {}
    void on_conflict() override {}
    bool full() const override { return false; }
    std::size_t num_learnts() const override { return num_learnts_; }
    void select(const reduce_context&, std::vector<std::uint8_t>&) const override {}
    void on_compact(const std::vector<std::uint8_t>&) override {}
    void on_reduced() override {}
    std::string_view name() const override { return "none"; }
};

/**
 * @brief activity + LBD deletion
 * when deleting, keeps original, locked, short (size <= 2) and low-LBD clauses
 * 
 */
class activity_lbd_deletion : public clause_deletion {
    double max_learnts_ = 0.0;
    double growth_;
    double floor_;

    // TODO: Track only learned clauses as we don't delete the original anyways

    // indicates the clause was learnt
    std::vector<bool> is_learnt_;
    // activity of the clause - increases when used
    std::vector<double> cla_activity_;
    // lbd
    std::vector<int> cla_lbd_;
    double cla_inc_ = 1.0;
    static constexpr double cla_decay_ = 0.999;

    // tmp for LBD (number of distinct decision levels in a clause).
    std::vector<int> lbd_stamp_;
    int lbd_gen_ = 0;

    std::size_t num_learnts_ = 0;

    void bump(std::size_t c) {
        if ((cla_activity_[c] += cla_inc_) > 1e100) {
            for (double& a : cla_activity_) a *= 1e-100;
            cla_inc_ *= 1e-100;
        }
    }

    /**
     * @brief 
     * 
     * @param clause clause we calculate lbd of
     * @param level_of decision levels of the variables
     * @return int 
     */
    int compute_lbd(const clause_t& clause, const std::vector<int>& level_of) {
        if (lbd_stamp_.size() < level_of.size() + 1)
            lbd_stamp_.resize(level_of.size() + 1, 0);
        ++lbd_gen_;
        int d = 0;
        for (const literal& l : clause) {
            std::size_t lv = static_cast<std::size_t>(level_of[l.get_id()]);
            if (lbd_stamp_[lv] != lbd_gen_) {
                lbd_stamp_[lv] = lbd_gen_;
                ++d;
            }
        }
        return d;
    }

public:
    explicit activity_lbd_deletion(double growth = 1.1, double floor = 100.0)
        : growth_(growth), floor_(floor) {}

    void start(std::size_t clause_count) override {
        max_learnts_ = static_cast<double>(clause_count) / 3.0;
        if (max_learnts_ < floor_) max_learnts_ = floor_;
        is_learnt_.assign(clause_count, false);
        cla_activity_.assign(clause_count, 0.0);
        cla_lbd_.assign(clause_count, 0);
        cla_inc_ = 1.0;
        lbd_stamp_.clear();
        lbd_gen_ = 0;
        num_learnts_ = 0;
    }

    /**
     * @brief 
     * 
     * @param c 
     * @param clause 
     * @param level_of decision levels of the variables
     */
    void on_learn(std::size_t c, const clause_t& clause,
                  const std::vector<int>& level_of) override {
        is_learnt_.push_back(true);
        cla_activity_.push_back(0.0);
        cla_lbd_.push_back(compute_lbd(clause, level_of));
        ++num_learnts_;
        bump(c);
    }

    void on_used(std::size_t c) override {
        if (is_learnt_[c]) bump(c);
    }

    void on_conflict() override { cla_inc_ *= (1.0 / cla_decay_); }

    bool full() const override {
        return static_cast<double>(num_learnts_) > max_learnts_;
    }

    std::size_t num_learnts() const override { return num_learnts_; }

    void select(const reduce_context& ctx, std::vector<std::uint8_t>& remove) const override {
        std::vector<std::size_t> cand;
        for (std::size_t c = 0; c < ctx.clauses.size(); ++c) {
            if (is_learnt_[c] && !ctx.locked[c] && ctx.clauses[c].size() > 2 && cla_lbd_[c] > 2)
                cand.push_back(c);
        }
        std::sort(cand.begin(), cand.end(), [&](std::size_t a, std::size_t b) {
            return cla_activity_[a] < cla_activity_[b];
        });
        std::size_t half = cand.size() / 2;
        for (std::size_t k = 0; k < half; ++k) remove[cand[k]] = 1;
    }

    void on_compact(const std::vector<std::uint8_t>& remove) override {
        std::vector<bool> new_is_learned;
        std::vector<double> new_cla_activity;
        std::vector<int> new_cla_lbd;
        std::size_t kept = 0;
        for (std::size_t c = 0; c < remove.size(); ++c) {
            if (remove[c]) continue;
            new_is_learned.push_back(is_learnt_[c]);
            new_cla_activity.push_back(cla_activity_[c]);
            new_cla_lbd.push_back(cla_lbd_[c]);
            if (is_learnt_[c]) ++kept;
        }
        is_learnt_.swap(new_is_learned);
        cla_activity_.swap(new_cla_activity);
        cla_lbd_.swap(new_cla_lbd);
        num_learnts_ = kept;
    }

    void on_reduced() override { max_learnts_ *= growth_; }

    std::string_view name() const override { return "activity-lbd"; }
};

#endif
