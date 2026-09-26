#pragma once

// Lua scripts keeping what outlives a session (docs/SCRIPTING.md "Saving"):
// a high score, what a player unlocked, the blocks someone built.
//
//   store.save("best", 120)              -- numbers, text, true/false, tables of those
//   local best = store.load("best", 0)   -- the second value is what you get when there's nothing
//   store.add("coins", 5)                -- counts up (from 0), returns the new count
//   store.remove("best")
//   for _, key in ipairs(store.keys("level.")) do ... end   -- keys starting with "level.", in order
//
// It is a kke::storage::Store underneath (docs/STORAGE.md), so the same
// script saves to a file on a PC, or to a server's database. Every game
// gets its own collection ("lua.<game>"): scripts of one game can't read
// or overwrite another's. Nothing is opened until a script first uses it.
// save and remove return true, or false and why (a full disk); load
// never fails a script: a value it can't read is the default.

#include "kke/ScriptVM.h"
#include "kke/storage/Store.h"

#include <memory>
#include <string>

namespace kke {

class ScriptStore {
public:
    static constexpr size_t kMaxValueBytes = 1024 * 1024; // per saved value, encoded
    static constexpr size_t kMaxKeys = 1000;              // what one store.keys() returns, at most

    // `url`: where (openStore); `game`: whose collection ("lua." + game,
    // folded to a-z 0-9 _ - .).
    ScriptStore(std::string url, const std::string& game);
    // Uses a store someone else owns (a server's) instead of opening one.
    ScriptStore(storage::Store& shared, const std::string& game);
    ~ScriptStore();

    // Adds store.* to `vm`. The ScriptStore must outlive the vm's use of it.
    void bind(ScriptVM& vm);

    const std::string& collection() const { return m_collection; }
    const std::string& url() const { return m_url; }
    // Null until first used, or when it couldn't be opened (error() says why).
    storage::Store* store();
    const std::string& error() const { return m_error; }

    // "lua." + `game`, made a valid collection name.
    static std::string collectionFor(const std::string& game);

private:
    std::string m_url;
    std::string m_collection;
    std::unique_ptr<storage::Store> m_owned;
    storage::Store* m_store = nullptr;
    bool m_tried = false;
    std::string m_error;
};

} // namespace kke
