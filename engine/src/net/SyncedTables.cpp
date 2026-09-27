#include "kke/net/SyncedTables.h"

#include <algorithm>
#include <cstdint>

namespace kke::net {

namespace {

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void putU16(std::vector<uint8_t>& out, size_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void putBytes(std::vector<uint8_t>& out, const std::string& s) { out.insert(out.end(), s.begin(), s.end()); }

// Reads what the writers above wrote; any short read fails the whole payload.
struct Reader {
    const std::vector<uint8_t>& in;
    size_t at = 0;
    bool ok = true;
    uint32_t u32() {
        if (at + 4 > in.size()) return fail();
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(in[at + size_t(i)]) << (8 * i);
        at += 4;
        return v;
    }
    size_t u16() {
        if (at + 2 > in.size()) return fail();
        const size_t v = size_t(in[at]) | (size_t(in[at + 1]) << 8);
        at += 2;
        return v;
    }
    uint8_t u8() {
        if (at + 1 > in.size()) return uint8_t(fail());
        return in[at++];
    }
    std::string bytes(size_t n) {
        if (at + n > in.size()) {
            fail();
            return {};
        }
        std::string s(in.begin() + std::ptrdiff_t(at), in.begin() + std::ptrdiff_t(at + n));
        at += n;
        return s;
    }
    uint32_t fail() {
        ok = false;
        at = in.size();
        return 0;
    }
    bool done() const { return ok && at == in.size(); }
};

bool validName(const std::string& name, size_t max) {
    return !name.empty() && name.size() <= max && name.find('\0') == std::string::npos;
}

// Ops for one player, packed into as few events as fit.
struct Packer {
    size_t max;
    std::vector<std::vector<uint8_t>> events;
    void add(uint32_t sub, tables_wire::Op op, const std::string& key, const std::string& row) {
        const size_t size = 4 + 1 + 2 + key.size() + 2 + row.size();
        if (events.empty() || events.back().size() + size > max) events.emplace_back();
        std::vector<uint8_t>& e = events.back();
        putU32(e, sub);
        e.push_back(op);
        putU16(e, key.size());
        putBytes(e, key);
        putU16(e, row.size());
        putBytes(e, row);
    }
};

} // namespace

// ---------------------------------------------------------------- wire

namespace tables_wire {

std::vector<uint8_t> subscribe(uint32_t sub, const std::string& table, const TableFilter& filter) {
    std::vector<uint8_t> out;
    putU32(out, sub);
    out.push_back(static_cast<uint8_t>(table.size()));
    putBytes(out, table);
    out.push_back(static_cast<uint8_t>(filter.size()));
    for (const auto& [field, value] : filter) {
        out.push_back(static_cast<uint8_t>(field.size()));
        putBytes(out, field);
        putU16(out, value.size());
        putBytes(out, value);
    }
    return out;
}

std::vector<uint8_t> unsubscribe(uint32_t sub) {
    std::vector<uint8_t> out;
    putU32(out, sub);
    return out;
}

std::vector<uint8_t> error(uint32_t sub, const std::string& why) {
    Packer p{ SIZE_MAX, {} };
    p.add(sub, Error, {}, why.substr(0, 400));
    return p.events.front();
}

std::optional<Subscribe> decodeSubscribe(const std::vector<uint8_t>& bytes, const TableLimits& limits) {
    Reader r{ bytes };
    Subscribe s;
    s.sub = r.u32();
    s.table = r.bytes(r.u8());
    const size_t n = r.u8();
    if (!r.ok || !validName(s.table, limits.maxNameBytes) || n > limits.maxFilterFields) return std::nullopt;
    for (size_t i = 0; i < n; ++i) {
        std::string field = r.bytes(r.u8());
        std::string value = r.bytes(r.u16());
        if (!r.ok || !validName(field, limits.maxNameBytes) || value.empty() || value.size() > limits.maxKeyBytes) return std::nullopt;
        s.filter.emplace_back(std::move(field), std::move(value));
    }
    if (!r.done()) return std::nullopt;
    return s;
}

std::optional<uint32_t> decodeUnsubscribe(const std::vector<uint8_t>& bytes) {
    Reader r{ bytes };
    const uint32_t sub = r.u32();
    if (!r.done()) return std::nullopt;
    return sub;
}

std::optional<std::vector<RowOp>> decodeRows(const std::vector<uint8_t>& bytes) {
    Reader r{ bytes };
    std::vector<RowOp> ops;
    while (r.ok && r.at < bytes.size()) {
        RowOp op;
        op.sub = r.u32();
        const uint8_t kind = r.u8();
        op.key = r.bytes(r.u16());
        op.row = r.bytes(r.u16());
        if (!r.ok || kind > Error) return std::nullopt;
        op.op = static_cast<Op>(kind);
        ops.push_back(std::move(op));
    }
    if (!r.done() || ops.empty()) return std::nullopt;
    return ops;
}

} // namespace tables_wire

// ---------------------------------------------------------------- server

void TableServer::touch(const std::string& table, const std::string& key) {
    const TableRow* now = get(table, key);
    auto& before = m_before[table];
    if (!before.count(key)) before.emplace(key, now ? std::optional<TableRow>(*now) : std::nullopt);
    if (m_undoing) {
        const auto k = std::make_pair(table, key);
        if (!m_undo.count(k)) m_undo.emplace(k, now ? std::optional<TableRow>(*now) : std::nullopt);
    }
}

bool TableServer::set(const std::string& table, const std::string& key, TableRow row, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (!validName(table, m_limits.maxNameBytes)) return fail("a table name is 1-" + std::to_string(m_limits.maxNameBytes) + " characters");
    if (key.empty() || key.size() > m_limits.maxKeyBytes) return fail("the key is too long (at most " + std::to_string(m_limits.maxKeyBytes) + " bytes)");
    if (row.bytes.size() > m_limits.maxRowBytes)
        return fail("the row is too big (" + std::to_string(row.bytes.size()) + " bytes, at most " + std::to_string(m_limits.maxRowBytes) + ")");
    const auto t = m_tables.find(table);
    if (t == m_tables.end() && m_tables.size() >= m_limits.maxTables) return fail("too many tables (at most " + std::to_string(m_limits.maxTables) + ")");
    if (t != m_tables.end() && !t->second.count(key) && t->second.size() >= m_limits.maxRowsPerTable)
        return fail("table '" + table + "' is full (at most " + std::to_string(m_limits.maxRowsPerTable) + " rows)");
    touch(table, key);
    m_tables[table][key] = std::move(row);
    return true;
}

bool TableServer::remove(const std::string& table, const std::string& key) {
    const auto t = m_tables.find(table);
    if (t == m_tables.end() || !t->second.count(key)) return false;
    touch(table, key);
    t->second.erase(key);
    return true;
}

void TableServer::clear(const std::string& table) {
    const auto t = m_tables.find(table);
    if (t == m_tables.end()) return;
    for (const auto& [key, row] : t->second) touch(table, key);
    m_tables.erase(t);
}

const TableRow* TableServer::get(const std::string& table, const std::string& key) const {
    const auto t = m_tables.find(table);
    if (t == m_tables.end()) return nullptr;
    const auto r = t->second.find(key);
    return r == t->second.end() ? nullptr : &r->second;
}

const std::map<std::string, TableRow>* TableServer::rows(const std::string& table) const {
    const auto t = m_tables.find(table);
    return t == m_tables.end() ? nullptr : &t->second;
}

bool TableServer::matches(const TableFilter& f, const TableRow& row) {
    for (const auto& [field, value] : f) {
        const auto it = row.fields.find(field);
        if (it == row.fields.end() || it->second != value) return false;
    }
    return true;
}

void TableServer::received(int player, uint16_t kind, const std::vector<uint8_t>& payload) {
    if (kind == kTableUnsubscribe) {
        if (const auto sub = tables_wire::decodeUnsubscribe(payload))
            std::erase_if(m_watches, [&](const Watch& w) { return w.player == player && w.id == *sub; });
        return;
    }
    if (kind != kTableSubscribe) return;
    const auto s = tables_wire::decodeSubscribe(payload, m_limits);
    if (!s) return; // no id to answer to
    std::erase_if(m_watches, [&](const Watch& w) { return w.player == player && w.id == s->sub; });
    Watch w{ player, s->sub, s->table, s->filter, true, {} };
    if (watches(player) >= m_limits.maxWatchesPerPlayer) w.error = "too many watches (at most " + std::to_string(m_limits.maxWatchesPerPlayer) + ")";
    m_watches.push_back(std::move(w));
}

void TableServer::dropPlayer(int player) {
    std::erase_if(m_watches, [&](const Watch& w) { return w.player == player; });
}

size_t TableServer::watches(int player) const {
    return size_t(std::count_if(m_watches.begin(), m_watches.end(), [&](const Watch& w) { return w.player == player && w.error.empty(); }));
}

std::vector<std::pair<int, std::vector<uint8_t>>> TableServer::flush() {
    std::map<int, Packer> out;
    auto packer = [&](int player) -> Packer& { return out.try_emplace(player, Packer{ m_limits.maxEventBytes, {} }).first->second; };
    // What changed since the last flush, for the watches that have their rows.
    for (const auto& [table, keys] : m_before) {
        for (const auto& [key, before] : keys) {
            const TableRow* now = get(table, key);
            if ((!before && !now) || (before && now && *before == *now)) continue;
            for (const Watch& w : m_watches) {
                if (w.fresh || !w.error.empty() || w.table != table) continue;
                const bool was = before && matches(w.filter, *before);
                const bool is = now && matches(w.filter, *now);
                if (was && is) packer(w.player).add(w.id, tables_wire::Update, key, now->bytes);
                else if (is) packer(w.player).add(w.id, tables_wire::Insert, key, now->bytes);
                else if (was) packer(w.player).add(w.id, tables_wire::Delete, key, {});
            }
        }
    }
    m_before.clear();
    // New watches: their rows as they are now, then ready. Refused ones: why.
    for (Watch& w : m_watches) {
        if (!w.error.empty()) {
            packer(w.player).add(w.id, tables_wire::Error, {}, w.error);
            continue;
        }
        if (!w.fresh) continue;
        w.fresh = false;
        if (const auto* rs = rows(w.table))
            for (const auto& [key, row] : *rs)
                if (matches(w.filter, row)) packer(w.player).add(w.id, tables_wire::Insert, key, row.bytes);
        packer(w.player).add(w.id, tables_wire::Ready, {}, {});
    }
    std::erase_if(m_watches, [](const Watch& w) { return !w.error.empty(); });
    std::vector<std::pair<int, std::vector<uint8_t>>> result;
    for (auto& [player, p] : out)
        for (auto& e : p.events) result.emplace_back(player, std::move(e));
    return result;
}

void TableServer::beginUndo() {
    m_undoing = true;
    m_undo.clear();
}

void TableServer::endUndo() {
    m_undoing = false;
    m_undo.clear();
}

void TableServer::rollback() {
    for (auto& [k, row] : m_undo) {
        if (row) {
            m_tables[k.first][k.second] = std::move(*row);
        } else if (auto t = m_tables.find(k.first); t != m_tables.end()) {
            t->second.erase(k.second);
            if (t->second.empty()) m_tables.erase(t);
        }
    }
    endUndo();
}

// ---------------------------------------------------------------- client

uint32_t TableClient::watch(const std::string& table, const TableFilter& filter) {
    if (!validName(table, m_limits.maxNameBytes) || filter.size() > m_limits.maxFilterFields) return 0;
    for (const auto& [field, value] : filter)
        if (!validName(field, m_limits.maxNameBytes) || value.empty() || value.size() > m_limits.maxKeyBytes) return 0;
    const uint32_t id = m_nextWatch++;
    Watch& w = m_watches[id];
    w.table = table;
    w.filter = filter;
    if (m_source) subscribe(id, w);
    return id;
}

void TableClient::subscribe(uint32_t id, Watch& w) {
    if (w.sub) m_subToWatch.erase(w.sub);
    w.sub = m_nextSub++;
    if (m_nextSub == 0) m_nextSub = 1;
    m_subToWatch[w.sub] = id;
    w.ready = false;
    w.incoming.clear();
    m_sends.emplace_back(kTableSubscribe, tables_wire::subscribe(w.sub, w.table, w.filter));
}

void TableClient::stop(uint32_t id) {
    const auto it = m_watches.find(id);
    if (it == m_watches.end()) return;
    if (it->second.sub) {
        m_subToWatch.erase(it->second.sub);
        if (m_source) m_sends.emplace_back(kTableUnsubscribe, tables_wire::unsubscribe(it->second.sub));
    }
    m_watches.erase(it);
    std::erase_if(m_changes, [id](const Change& c) { return c.watch == id; });
}

bool TableClient::ready(uint32_t id) const {
    const auto it = m_watches.find(id);
    return it != m_watches.end() && it->second.ready;
}

const std::map<std::string, std::string>* TableClient::rows(uint32_t id) const {
    const auto it = m_watches.find(id);
    return it == m_watches.end() ? nullptr : &it->second.rows;
}

void TableClient::received(const std::vector<uint8_t>& payload) {
    const auto ops = tables_wire::decodeRows(payload);
    if (!ops) return;
    for (const tables_wire::RowOp& op : *ops) {
        const auto s = m_subToWatch.find(op.sub);
        if (s == m_subToWatch.end()) continue; // an old subscription's
        const uint32_t id = s->second;
        apply(m_watches[id], id, op.op, op.key, op.row);
    }
}

void TableClient::apply(Watch& w, uint32_t id, uint8_t op, std::string key, std::string row) {
    using K = Change::Kind;
    if (op == tables_wire::Error) {
        m_subToWatch.erase(w.sub);
        w.sub = 0;
        w.ready = false;
        m_changes.push_back({ id, K::Error, {}, std::move(row), {} });
        return;
    }
    if (!w.ready) {
        // A subscription's first rows: gathered, then compared with what we had.
        if (op == tables_wire::Insert || op == tables_wire::Update) w.incoming[key] = std::move(row);
        else if (op == tables_wire::Delete) w.incoming.erase(key);
        if (op != tables_wire::Ready) return;
        for (const auto& [k, r] : w.rows)
            if (!w.incoming.count(k)) m_changes.push_back({ id, K::Delete, k, {}, r });
        for (const auto& [k, r] : w.incoming) {
            const auto old = w.rows.find(k);
            if (old == w.rows.end()) m_changes.push_back({ id, K::Insert, k, r, {} });
            else if (old->second != r) m_changes.push_back({ id, K::Update, k, r, old->second });
        }
        w.rows = std::move(w.incoming);
        w.incoming.clear();
        w.ready = true;
        m_changes.push_back({ id, K::Ready, {}, {}, {} });
        return;
    }
    if (op == tables_wire::Insert || op == tables_wire::Update) {
        const auto old = w.rows.find(key);
        if (old == w.rows.end()) {
            m_changes.push_back({ id, K::Insert, key, row, {} });
            w.rows.emplace(std::move(key), std::move(row));
        } else if (old->second != row) {
            m_changes.push_back({ id, K::Update, key, row, old->second });
            old->second = std::move(row);
        }
    } else if (op == tables_wire::Delete) {
        const auto old = w.rows.find(key);
        if (old == w.rows.end()) return;
        m_changes.push_back({ id, K::Delete, key, {}, old->second });
        w.rows.erase(old);
    }
}

void TableClient::resubscribe() {
    m_source = true;
    for (auto& [id, w] : m_watches) subscribe(id, w);
}

void TableClient::sourceLost() {
    m_source = false;
    m_sends.clear();
    m_subToWatch.clear();
    for (auto& [id, w] : m_watches) {
        w.sub = 0;
        w.ready = false;
        w.incoming.clear();
    }
}

std::vector<std::pair<uint16_t, std::vector<uint8_t>>> TableClient::takeSends() {
    auto out = std::move(m_sends);
    m_sends.clear();
    return out;
}

std::vector<TableClient::Change> TableClient::takeChanges() {
    auto out = std::move(m_changes);
    m_changes.clear();
    return out;
}

} // namespace kke::net
