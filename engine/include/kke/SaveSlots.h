#pragma once

// Save slots (ENG-F53, docs/SAVING.md): the "Continue / Load game" menu's
// files. Each slot holds one save (the game's own bytes) with what a menu
// shows about it: a title, when it was saved, how long the player has
// played, and the game's save version, so an old save can be brought up to
// date when it is read (Lua: saves.migrate) and a newer one is refused
// instead of read wrongly.
//
// A save is one self-checking file (magic, header, data, CRC-32), kept in a
// kke::storage::Store (docs/STORAGE.md): a SQLite file next to the game by
// default, so a write is all or nothing. The same file is what a cloud hook
// uploads (onWritten) and hands back (importFile), and importFile only takes
// it when it is intact and newer than the slot's own save.

#include "kke/storage/Store.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kke {

struct SaveInfo {
    std::string slot;
    std::string title;          // what the menu shows ("Chapter 2: the docks")
    int64_t savedAtMs = 0;      // Unix time in milliseconds
    uint32_t version = 1;       // the game's save version when it was written
    double playSeconds = 0.0;   // the game's play time, as it reported it
    uint64_t dataBytes = 0;
};

struct SaveFile {
    SaveInfo info;
    std::string data;
};

class SaveSlots {
public:
    static constexpr size_t kMaxSlots = 100;
    static constexpr size_t kMaxSlotName = 64;
    static constexpr size_t kMaxTitle = 128;
    static constexpr size_t kMaxData = 8 * 1024 * 1024;

    // `game` names the collection ("saves." + game, folded to a-z 0-9 _ - .).
    SaveSlots(storage::Store& store, const std::string& game);

    const std::string& collection() const { return m_collection; }

    // The version write() stamps (1 by default). Raise it when the shape of
    // what the game saves changes.
    void setVersion(uint32_t version) { m_version = version ? version : 1; }
    uint32_t version() const { return m_version; }

    // Writes `data` to `slot` (1-64 characters: letters, digits, space _ - .),
    // replacing what was there. False with a reason: a bad name, too much
    // data, kMaxSlots slots already used, or the store failed.
    bool write(const std::string& slot, const std::string& data, const std::string& title, double playSeconds, std::string* error = nullptr);
    // What `slot` holds, or nothing with a reason (empty, damaged). The
    // version is the one it was saved with; the caller decides what to do
    // with an older or newer one.
    std::optional<SaveFile> read(const std::string& slot, std::string* error = nullptr);
    // Every slot's info, newest first (damaged ones are left out).
    std::vector<SaveInfo> list();
    bool remove(const std::string& slot, std::string* error = nullptr);
    bool exists(const std::string& slot);

    // The cloud hook. onWritten gets the slot's whole file after each write
    // (upload it as it is); onRemoved the slot's name after a remove.
    std::function<void(const SaveInfo&, const std::string& file)> onWritten;
    std::function<void(const std::string& slot)> onRemoved;
    // The slot's whole file (to upload one that was written before the hook
    // was set), or nothing.
    std::optional<std::string> fileOf(const std::string& slot);
    // A file from elsewhere (a download): kept in its slot when it is intact
    // and saved later than what the slot holds (or the slot is empty).
    enum class Import { Taken, Older, Damaged, Failed };
    Import importFile(const std::string& file, std::string* error = nullptr);

    // The file format, for tests and tools.
    static std::string encode(const SaveInfo& info, const std::string& data);
    static std::optional<SaveFile> decode(const std::string& file, std::string* error = nullptr);
    static bool validSlot(const std::string& slot);
    static uint32_t crc32(const void* data, size_t size);

    // The clock write() stamps with (milliseconds since 1970); tests set it.
    std::function<int64_t()> now;

private:
    std::string key(const std::string& slot) const { return "slot." + slot; }
    bool fail(std::string* error, std::string message) const;

    storage::Store& m_store;
    std::string m_collection;
    uint32_t m_version = 1;
};

} // namespace kke
