#include "kke/GameRules.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kke {

namespace {

const char* opText(RuleOp op) {
    switch (op) {
    case RuleOp::Less: return "<";
    case RuleOp::LessEqual: return "<=";
    case RuleOp::Equal: return "==";
    case RuleOp::NotEqual: return "!=";
    case RuleOp::GreaterEqual: return ">=";
    case RuleOp::Greater: return ">";
    }
    return "?";
}

double factOf(const Facts& facts, const std::string& name) {
    auto it = facts.find(name);
    return it == facts.end() ? 0.0 : it->second;
}

std::string number(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    if (!std::isalpha(static_cast<unsigned char>(s[0])) && s[0] != '_') return false;
    for (char c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.') return false;
    return true;
}

void fail(std::string* error, const std::string& what) {
    if (error) *error = what;
}

} // namespace

bool Condition::test(const Facts& facts) const {
    const double a = factOf(facts, fact);
    const double b = valueFact.empty() ? value : factOf(facts, valueFact);
    if (!std::isfinite(a) || !std::isfinite(b)) return false; // NaN or inf never satisfies a rule
    switch (op) {
    case RuleOp::Less: return a < b;
    case RuleOp::LessEqual: return a <= b;
    case RuleOp::Equal: return a == b;
    case RuleOp::NotEqual: return a != b;
    case RuleOp::GreaterEqual: return a >= b;
    case RuleOp::Greater: return a > b;
    }
    return false;
}

std::string Condition::toString() const {
    return fact + " " + opText(op) + " " + (valueFact.empty() ? number(value) : valueFact);
}

bool Condition::parse(const std::string& text, Condition& out, std::string* error) {
    // fact, op, rhs separated by optional spaces.
    size_t i = 0;
    auto skip = [&] { while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i; };
    auto word = [&] {
        const size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && std::string("<>=!").find(text[i]) == std::string::npos) ++i;
        return text.substr(start, i - start);
    };
    Condition c;
    skip();
    c.fact = word();
    if (!validName(c.fact)) {
        fail(error, "\"" + text + "\": a condition starts with a fact name");
        return false;
    }
    skip();
    static const std::pair<const char*, RuleOp> ops[] = { { "<=", RuleOp::LessEqual }, { ">=", RuleOp::GreaterEqual }, { "==", RuleOp::Equal },
                                                          { "!=", RuleOp::NotEqual },  { "<", RuleOp::Less },          { ">", RuleOp::Greater } };
    bool found = false;
    for (const auto& [t, op] : ops) {
        const size_t n = std::char_traits<char>::length(t);
        if (text.compare(i, n, t) == 0) {
            c.op = op;
            i += n;
            found = true;
            break;
        }
    }
    if (!found) {
        fail(error, "\"" + text + "\": expected one of < <= == != >= >");
        return false;
    }
    skip();
    const std::string rhs = word();
    skip();
    if (rhs.empty() || i != text.size()) {
        fail(error, "\"" + text + "\": expected a number or a fact after the comparison");
        return false;
    }
    char* end = nullptr;
    const double v = std::strtod(rhs.c_str(), &end);
    if (end && *end == 0 && std::isfinite(v)) {
        c.value = v;
    } else if (validName(rhs)) {
        c.valueFact = rhs;
    } else {
        fail(error, "\"" + text + "\": \"" + rhs + "\" is neither a number nor a fact name");
        return false;
    }
    out = c;
    return true;
}

bool GameRules::add(const Rule& rule) {
    if (rule.name.empty() || rule.require.empty()) return false;
    for (const std::vector<Condition>* list : { &rule.when, &rule.require })
        for (const Condition& c : *list)
            if (!validName(c.fact) || (!c.valueFact.empty() && !validName(c.valueFact)) || !std::isfinite(c.value)) return false;
    m_rules.push_back(rule);
    return true;
}

bool GameRules::limit(const std::string& name, const std::string& fact, double min, double max) {
    if (!validName(fact) || !(min <= max)) return false;
    Rule r;
    r.name = name;
    r.require.push_back({ fact, RuleOp::GreaterEqual, min, {} });
    r.require.push_back({ fact, RuleOp::LessEqual, max, {} });
    return add(r);
}

bool GameRules::rule(const std::string& name, const std::vector<std::string>& when, const std::vector<std::string>& require, std::string* error) {
    Rule r;
    r.name = name;
    for (const std::string& t : when) {
        Condition c;
        if (!Condition::parse(t, c, error)) return false;
        r.when.push_back(c);
    }
    for (const std::string& t : require) {
        Condition c;
        if (!Condition::parse(t, c, error)) return false;
        r.require.push_back(c);
    }
    if (!add(r)) {
        fail(error, "a rule needs a name and at least one requirement");
        return false;
    }
    return true;
}

bool GameRules::check(const Facts& facts, std::vector<RuleViolation>* violations) {
    ++m_checks;
    bool ok = true;
    for (const Rule& r : m_rules) {
        bool applies = true;
        for (const Condition& c : r.when) applies = applies && c.test(facts);
        if (!applies) continue;
        for (const Condition& c : r.require) {
            if (c.test(facts)) continue;
            ok = false;
            ++m_violations[r.name];
            if (violations) {
                std::string seen = c.fact + " = " + number(factOf(facts, c.fact));
                if (!c.valueFact.empty()) seen += ", " + c.valueFact + " = " + number(factOf(facts, c.valueFact));
                violations->push_back({ r.name, "needs " + c.toString() + " (" + seen + ")" });
            }
            break; // one report per rule
        }
    }
    return ok;
}

size_t GameRules::violations(const std::string& rule) const {
    auto it = m_violations.find(rule);
    return it == m_violations.end() ? 0 : it->second;
}

void GameRules::clear() {
    m_rules.clear();
    m_violations.clear();
    m_checks = 0;
}

std::string GameRules::toJson() const {
    nlohmann::ordered_json rules = nlohmann::ordered_json::array();
    for (const Rule& r : m_rules) {
        nlohmann::ordered_json j;
        j["name"] = r.name;
        if (!r.when.empty()) {
            j["when"] = nlohmann::ordered_json::array();
            for (const Condition& c : r.when) j["when"].push_back(c.toString());
        }
        j["require"] = nlohmann::ordered_json::array();
        for (const Condition& c : r.require) j["require"].push_back(c.toString());
        rules.push_back(j);
    }
    nlohmann::ordered_json root;
    root["rules"] = rules;
    return root.dump(2) + "\n";
}

bool GameRules::loadJson(const std::string& text, std::string* error) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("rules") || !j["rules"].is_array()) {
        fail(error, "expected {\"rules\": [...]}");
        return false;
    }
    GameRules loaded;
    for (const nlohmann::json& r : j["rules"]) {
        if (!r.is_object() || !r.contains("name") || !r["name"].is_string() || !r.contains("require") || !r["require"].is_array()) {
            fail(error, "each rule needs a \"name\" and a \"require\" list");
            return false;
        }
        std::vector<std::string> when, require;
        auto strings = [&](const nlohmann::json& list, std::vector<std::string>& out) {
            for (const nlohmann::json& s : list) {
                if (!s.is_string()) return false;
                out.push_back(s.get<std::string>());
            }
            return true;
        };
        if ((r.contains("when") && (!r["when"].is_array() || !strings(r["when"], when))) || !strings(r["require"], require)) {
            fail(error, "rule \"" + r["name"].get<std::string>() + "\": conditions are strings like \"fuel > 0\"");
            return false;
        }
        if (!loaded.rule(r["name"].get<std::string>(), when, require, error)) return false;
    }
    m_rules = std::move(loaded.m_rules);
    return true;
}

} // namespace kke
