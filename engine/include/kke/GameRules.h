#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

// A game's "business rules" (docs/ANTI_CHEAT.md "Game rules"): what can't
// happen in this game unless someone injected code or typed a console
// command. The developer states them as plain facts, the server checks
// every claim a client makes against them:
//
//   "Flying needs a plane with fuel"
//        when    flying == 1
//        require in_plane == 1, fuel > 0
//   "Nobody hits for more than the best buffed attack"
//        require damage <= max_damage       (max_damage: a fact the game
//                                            fills in, multipliers included)
//
// A fact is a named number the game fills in for the moment being checked
// (a missing fact is 0; true/false are 1/0). Conditions compare a fact
// with a number or with another fact. Rules are data: JSON here, and the
// drag-and-drop and node tools can build the same rules (docs/PLAY_TO_MAKE.md).
using Facts = std::map<std::string, double>;

enum class RuleOp : uint8_t { Less, LessEqual, Equal, NotEqual, GreaterEqual, Greater };

struct Condition {
    std::string fact;
    RuleOp op = RuleOp::Equal;
    double value = 0.0;
    std::string valueFact; // when set, compare with this fact instead of `value`

    bool test(const Facts& facts) const;
    std::string toString() const; // "fuel > 0", "damage <= max_damage"
    // Parses "fact op number" or "fact op fact", op one of < <= == != >= >.
    static bool parse(const std::string& text, Condition& out, std::string* error = nullptr);
};

struct Rule {
    std::string name;               // what a person reads in a report
    std::vector<Condition> when;    // all true: the rule applies (empty = always)
    std::vector<Condition> require; // then all of these must hold
};

struct RuleViolation {
    std::string rule;
    std::string detail; // the first failed requirement with the values seen
};

class GameRules {
public:
    // Adds a rule; false (and nothing added) without a name or requirements.
    bool add(const Rule& rule);
    // Shorthand: min <= fact <= max, always.
    bool limit(const std::string& name, const std::string& fact, double min, double max);
    // Shorthand from text: rule("fly needs a plane", {"flying == 1"}, {"in_plane == 1", "fuel > 0"}).
    bool rule(const std::string& name, const std::vector<std::string>& when, const std::vector<std::string>& require,
              std::string* error = nullptr);

    // True when every rule holds; the broken ones go to `violations`.
    bool check(const Facts& facts, std::vector<RuleViolation>* violations = nullptr);

    const std::vector<Rule>& rules() const { return m_rules; }
    size_t violations(const std::string& rule) const; // broken so far
    size_t checks() const { return m_checks; }
    void clear();

    // {"rules": [{"name": "...", "when": ["flying == 1"], "require": ["fuel > 0"]}]}
    std::string toJson() const;
    // Replaces the rules; false (rules unchanged) with `error` on any bad rule.
    bool loadJson(const std::string& json, std::string* error = nullptr);

private:
    std::vector<Rule> m_rules;
    std::map<std::string, size_t> m_violations;
    size_t m_checks = 0;
};

} // namespace kke
