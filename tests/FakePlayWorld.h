#pragma once

// The world the play blocks act on, as a list: kke::IPlayWorld for tests
// that run play.* Lua (test_node_graph.cpp, test_cookbook.cpp).

#include "kke/PlayScript.h"

#include <glm/glm.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

class FakePlayWorld : public kke::IPlayWorld {
public:
    struct Thing { std::string block, owner; glm::vec3 pos{0.0f}; bool down = false; };
    std::map<uint32_t, Thing> things;
    uint32_t next = 1;
    std::vector<std::pair<uint32_t, glm::vec3>> ragdolls;
    std::vector<std::string> sounds, said, removedOwners;
    double points = 0.0;

    uint32_t add(const std::string& block, glm::vec3 pos = glm::vec3(0.0f)) {
        things[next] = { block, "", pos, false };
        return next++;
    }

    std::vector<BlockInfo> blocks() const override { return { { "person", "Person", "character" }, { "crate", "Box", "prop" } }; }
    uint32_t spawn(const std::string& block, const glm::vec3& p, float, const std::string& owner) override {
        if (block != "person" && block != "crate") return 0;
        things[next] = { block, owner, p, false };
        return next++;
    }
    bool remove(uint32_t t) override { return things.erase(t) > 0; }
    bool exists(uint32_t t) const override { return things.count(t) > 0; }
    bool ragdoll(uint32_t t, const glm::vec3& push) override {
        auto it = things.find(t);
        if (it == things.end() || it->second.block != "person" || it->second.down) return false;
        it->second.down = true;
        ragdolls.emplace_back(t, push);
        return true;
    }
    bool standUp(uint32_t t) override {
        auto it = things.find(t);
        if (it == things.end() || !it->second.down) return false;
        it->second.down = false;
        return true;
    }
    bool isDown(uint32_t t) const override { return things.count(t) && things.at(t).down; }
    bool swingAt(uint32_t t) override { return exists(t); }
    void sound(const std::string& name, const glm::vec3&) override { sounds.push_back(name); }
    void say(const std::string& text) override { said.push_back(text); }
    double addScore(double p) override { return points += p; }
    double score() const override { return points; }
    glm::vec3 position(uint32_t t) const override { return things.count(t) ? things.at(t).pos : glm::vec3(0.0f); }
    std::string blockOf(uint32_t t) const override { return things.count(t) ? things.at(t).block : std::string(); }
    void removeOwnedBy(const std::string& owner) override {
        removedOwners.push_back(owner);
        std::erase_if(things, [&](const auto& kv) { return kv.second.owner == owner; });
    }
};
