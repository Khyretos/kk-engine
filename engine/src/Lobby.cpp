#include "kke/Lobby.h"

#include <algorithm>

namespace kke {

namespace {
constexpr const char* kCpus = "cpus";
std::string cpuId(int cpu) { return "cpu." + std::to_string(cpu + 1); }
} // namespace

Lobby::Lobby() : m_seats(kMaxSeats) {
    m_seats[0].joined = true;
    Option cpus;
    cpus.id = kCpus;
    cpus.label = "CPU players";
    for (int i = 0; i <= kMaxCpus; ++i) cpus.choices.push_back(std::to_string(i));
    m_options.push_back(std::move(cpus));
    for (int i = 0; i < kMaxCpus; ++i) {
        Option cpu;
        cpu.id = cpuId(i);
        cpu.label = "CPU " + std::to_string(i + 1);
        m_options.push_back(std::move(cpu));
    }
    setDifficulties({ "Easy", "Normal", "Hard", "Expert" });
}

void Lobby::addLookField(LookField field) {
    const int n = static_cast<int>(field.choices.size());
    m_looks.push_back(std::move(field));
    // Every seat starts on a different choice (four different colours).
    for (size_t s = 0; s < m_seats.size(); ++s) m_seats[s].look.push_back(n > 0 ? static_cast<int>(s) % n : 0);
    changed();
}

int Lobby::addOption(Option option) {
    option.value = option.choices.empty() ? 0 : std::clamp(option.value, 0, static_cast<int>(option.choices.size()) - 1);
    m_options.push_back(std::move(option));
    changed();
    return static_cast<int>(m_options.size()) - 1;
}

Lobby::Option* Lobby::option(const std::string& id) {
    for (Option& o : m_options)
        if (o.id == id) return &o;
    return nullptr;
}

void Lobby::setDifficulties(std::vector<std::string> names) {
    if (names.empty()) names = { "Normal" };
    m_difficulties = std::move(names);
    const int normal = m_difficulties.size() > 1 ? 1 : 0;
    for (int i = 0; i < kMaxCpus; ++i) {
        Option* o = option(cpuId(i));
        o->choices = m_difficulties;
        o->value = normal;
    }
    refreshCpuRows();
}

void Lobby::setMaxCpus(int count) {
    m_maxCpus = std::clamp(count, 0, kMaxCpus);
    Option* o = option(kCpus);
    o->choices.clear();
    for (int i = 0; i <= m_maxCpus; ++i) o->choices.push_back(std::to_string(i));
    o->value = std::min(o->value, m_maxCpus);
    refreshCpuRows();
}

void Lobby::refreshCpuRows() {
    Option* count = option(kCpus);
    count->visible = m_maxCpus > 0;
    for (int i = 0; i < kMaxCpus; ++i) option(cpuId(i))->visible = i < count->value;
    changed();
}

int Lobby::cpuCount() const {
    return m_maxCpus > 0 ? m_options[0].value : 0;
}

void Lobby::setCpuCount(int count) {
    Option* o = option(kCpus);
    const int before = o->value;
    o->value = std::clamp(count, 0, m_maxCpus);
    // A new CPU player starts as hard as the one before it.
    for (int i = std::max(before, 1); i < o->value; ++i) option(cpuId(i))->value = option(cpuId(i - 1))->value;
    refreshCpuRows();
}

int Lobby::cpuDifficulty(int cpu) const {
    if (cpu < 0 || cpu >= kMaxCpus) return 0;
    return m_options[static_cast<size_t>(cpu) + 1].value;
}

void Lobby::setCpuDifficulty(int cpu, int difficulty) {
    if (cpu < 0 || cpu >= kMaxCpus) return;
    option(cpuId(cpu))->value = std::clamp(difficulty, 0, static_cast<int>(m_difficulties.size()) - 1);
    changed();
}

int Lobby::joinedCount() const {
    return static_cast<int>(std::count_if(m_seats.begin(), m_seats.end(), [](const Seat& s) { return s.joined; }));
}

std::vector<int> Lobby::joinedSeats() const {
    std::vector<int> out;
    for (size_t i = 0; i < m_seats.size(); ++i)
        if (m_seats[i].joined) out.push_back(static_cast<int>(i));
    return out;
}

int Lobby::seatOfPad(uint32_t pad) const {
    for (size_t i = 0; i < m_seats.size(); ++i)
        if (m_seats[i].joined && m_seats[i].device == Device::Pad && m_seats[i].pad == pad) return static_cast<int>(i);
    return -1;
}

int Lobby::seatOfKeyboard() const {
    for (size_t i = 0; i < m_seats.size(); ++i)
        if (m_seats[i].joined && m_seats[i].device == Device::KeyboardMouse) return static_cast<int>(i);
    return -1;
}

int Lobby::join(Device device, uint32_t pad) {
    if (device == Device::Pad ? seatOfPad(pad) >= 0 : device == Device::KeyboardMouse && seatOfKeyboard() >= 0) return -1;
    for (size_t i = 1; i < m_seats.size(); ++i) {
        Seat& s = m_seats[i];
        if (s.joined) continue;
        s.joined = true;
        s.device = device;
        s.pad = device == Device::Pad ? pad : 0;
        s.padPresent = true;
        s.row = 0;
        const int seat = static_cast<int>(i);
        toast(seatName(seat) + " joined");
        changed();
        if (onJoin) onJoin(seat);
        return seat;
    }
    return -1;
}

void Lobby::leave(int seat) {
    if (seat <= 0 || seat >= kMaxSeats || !m_seats[static_cast<size_t>(seat)].joined) return;
    const std::string who = seatName(seat);
    Seat& s = m_seats[static_cast<size_t>(seat)];
    s.joined = false;
    s.device = Device::Any;
    s.pad = 0;
    s.row = 0;
    toast(who + " left");
    changed();
    if (onLeave) onLeave(seat);
}

int Lobby::seatOfDevice(Device device, uint32_t pad) const {
    if (device == Device::Pad) return seatOfPad(pad);
    if (device == Device::KeyboardMouse) return seatOfKeyboard();
    return -1;
}

bool Lobby::setSeatDevice(int seat, Device device, uint32_t pad) {
    if (seat < 0 || seat >= kMaxSeats || device == Device::Any) return false;
    Seat& s = m_seats[static_cast<size_t>(seat)];
    if (!s.joined) return false;
    const int holder = seatOfDevice(device, pad);
    if (holder == seat) return true;
    if (holder >= 0) return false;
    s.device = device;
    s.pad = device == Device::Pad ? pad : 0;
    s.padPresent = true;
    changed();
    return true;
}

std::string Lobby::seatName(int seat) const {
    for (size_t f = 0; f < m_looks.size(); ++f) {
        const LookField& field = m_looks[f];
        if (field.id != "name" || field.choices.empty()) continue;
        const int c = m_seats[static_cast<size_t>(seat)].look[f];
        return field.choices[static_cast<size_t>(std::clamp(c, 0, static_cast<int>(field.choices.size()) - 1))];
    }
    return "Player " + std::to_string(seat + 1);
}

std::vector<Lobby::Row> Lobby::rows(int seat) const {
    std::vector<Row> out;
    for (size_t f = 0; f < m_looks.size(); ++f) out.push_back({ Row::Kind::Look, static_cast<int>(f) });
    if (seat == 0) {
        for (size_t o = 0; o < m_options.size(); ++o)
            if (m_options[o].visible) out.push_back({ Row::Kind::Option, static_cast<int>(o) });
        out.push_back({ Row::Kind::Start, 0 });
    }
    return out;
}

void Lobby::setLook(int seat, int field, int choice) {
    if (seat < 0 || seat >= kMaxSeats || field < 0 || field >= static_cast<int>(m_looks.size())) return;
    const int n = static_cast<int>(m_looks[static_cast<size_t>(field)].choices.size());
    if (n == 0) return;
    m_seats[static_cast<size_t>(seat)].look[static_cast<size_t>(field)] = ((choice % n) + n) % n;
    changed();
}

std::vector<uint32_t> Lobby::devicesFor(int seat, const std::vector<uint32_t>& pads, const std::vector<uint32_t>& keyboardMice) const {
    const Seat& s = m_seats[static_cast<size_t>(seat)];
    if (!s.joined) return { kNoDevice };
    if (s.device == Device::Pad) return { s.pad };
    if (s.device == Device::KeyboardMouse) return keyboardMice.empty() ? std::vector<uint32_t>{ kNoDevice } : keyboardMice;
    // Not pinned yet: everything nobody else has.
    std::vector<uint32_t> out;
    bool othersClaim = false;
    for (uint32_t p : pads) {
        if (seatOfPad(p) >= 0) othersClaim = true;
        else out.push_back(p);
    }
    const int kb = seatOfKeyboard();
    if (kb >= 0 && kb != seat) othersClaim = true;
    else out.insert(out.end(), keyboardMice.begin(), keyboardMice.end());
    if (!othersClaim) return {}; // every device
    return out.empty() ? std::vector<uint32_t>{ kNoDevice } : out;
}

void Lobby::setOpen(bool open) {
    if (m_open == open) return;
    m_open = open;
    for (Seat& s : m_seats) s.row = 0;
    changed();
}

void Lobby::handle(const Press& press, const std::vector<uint32_t>& connectedPads) {
    if (!press.any()) return;
    const bool pad = press.device == Device::Pad;
    int seat = pad ? seatOfPad(press.pad) : seatOfKeyboard();
    Seat& one = m_seats[0];
    if (seat < 0 && one.device == Device::Any) {
        if (m_open) {
            // Player 1's first press: that device is theirs from now on.
            one.device = press.device;
            one.pad = pad ? press.pad : 0;
            one.padPresent = true;
            changed();
            return;
        }
        // In game without a menu first: player 1 has the keyboard and the
        // first controller; any other controller can join.
        uint32_t firstFree = kNoDevice;
        for (uint32_t p : connectedPads)
            if (seatOfPad(p) < 0) {
                firstFree = p;
                break;
            }
        if (!pad || press.pad == firstFree) return;
    }
    if (seat < 0) {
        if (press.confirm) join(press.device, press.pad);
        return;
    }
    if (m_open) step(seat, press);
}

void Lobby::step(int seat, const Press& press) {
    Seat& s = m_seats[static_cast<size_t>(seat)];
    const std::vector<Row> list = rows(seat);
    const int last = static_cast<int>(list.size()) - 1;
    s.row = std::clamp(s.row, 0, std::max(0, last));
    if (press.back) {
        if (seat == 0) s.row = 0;
        else leave(seat);
        changed();
        return;
    }
    if (press.start && seat == 0) {
        m_start = true;
        return;
    }
    if (last < 0) return;
    if (press.up) s.row = std::max(0, s.row - 1);
    if (press.down) s.row = std::min(last, s.row + 1);
    const Row row = list[static_cast<size_t>(s.row)];
    const int delta = (press.right ? 1 : 0) - (press.left ? 1 : 0);
    if (delta != 0 && row.kind == Row::Kind::Look) setLook(seat, row.index, s.look[static_cast<size_t>(row.index)] + delta);
    if (delta != 0 && row.kind == Row::Kind::Option) {
        Option& o = m_options[static_cast<size_t>(row.index)];
        if (!o.choices.empty()) {
            const int before = o.value;
            if (o.id == kCpus) setCpuCount(o.value + delta);
            else o.value = std::clamp(o.value + delta, 0, static_cast<int>(o.choices.size()) - 1);
            if (o.value != before && o.onChange) o.onChange(o.value);
        }
    }
    if (press.confirm) {
        if (row.kind == Row::Kind::Start) m_start = true;
        else if (row.kind == Row::Kind::Option && m_options[static_cast<size_t>(row.index)].choices.empty()) {
            if (m_options[static_cast<size_t>(row.index)].onPress) m_options[static_cast<size_t>(row.index)].onPress();
        } else {
            s.row = std::min(static_cast<int>(rows(seat).size()) - 1, s.row + 1);
        }
    }
    changed();
}

void Lobby::padConnected(uint32_t pad, bool atStartup) {
    const int seat = seatOfPad(pad);
    if (seat >= 0) {
        m_seats[static_cast<size_t>(seat)].padPresent = true;
        toast(seatName(seat) + "'s controller is back");
        return;
    }
    if (atStartup || joinedCount() >= kMaxSeats) return;
    toast("Controller connected: press " + joinButton + " to join");
}

void Lobby::padDisconnected(uint32_t pad) {
    const int seat = seatOfPad(pad);
    if (seat < 0) return;
    m_seats[static_cast<size_t>(seat)].padPresent = false;
    toast(seatName(seat) + "'s controller is unplugged: plug it back in to carry on", 6.0f);
}

void Lobby::update(float dt) {
    const size_t before = m_toasts.size();
    for (Toast& t : m_toasts) t.ttl -= dt;
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(), [](const Toast& t) { return t.ttl <= 0.0f; }), m_toasts.end());
    if (m_toasts.size() != before) changed();
}

bool Lobby::takeStart() {
    const bool s = m_start;
    m_start = false;
    return s;
}

void Lobby::toast(const std::string& text, float seconds) {
    m_toasts.push_back({ text, seconds });
    if (m_toasts.size() > 4) m_toasts.erase(m_toasts.begin());
    changed();
}

nlohmann::json Lobby::save() const {
    nlohmann::json j;
    j["seats"] = nlohmann::json::array();
    for (const Seat& s : m_seats) {
        nlohmann::json look = nlohmann::json::object();
        for (size_t f = 0; f < m_looks.size(); ++f)
            if (!m_looks[f].choices.empty()) look[m_looks[f].id] = m_looks[f].choices[static_cast<size_t>(s.look[f])];
        j["seats"].push_back({ { "look", look } });
    }
    nlohmann::json options = nlohmann::json::object();
    for (const Option& o : m_options)
        if (!o.choices.empty()) options[o.id] = o.choices[static_cast<size_t>(o.value)];
    j["options"] = options;
    return j;
}

void Lobby::load(const nlohmann::json& j) {
    if (!j.is_object()) return;
    // By name, so a list that changed order (or lost a choice) still loads.
    auto indexOf = [](const std::vector<std::string>& choices, const nlohmann::json& v) {
        if (!v.is_string()) return -1;
        const auto it = std::find(choices.begin(), choices.end(), v.get<std::string>());
        return it == choices.end() ? -1 : static_cast<int>(it - choices.begin());
    };
    if (const auto seats = j.find("seats"); seats != j.end() && seats->is_array())
        for (size_t s = 0; s < seats->size() && s < m_seats.size(); ++s) {
            const nlohmann::json& look = (*seats)[s].value("look", nlohmann::json::object());
            if (!look.is_object()) continue;
            for (size_t f = 0; f < m_looks.size(); ++f)
                if (const auto v = look.find(m_looks[f].id); v != look.end())
                    if (const int c = indexOf(m_looks[f].choices, *v); c >= 0) m_seats[s].look[f] = c;
        }
    if (const auto options = j.find("options"); options != j.end() && options->is_object()) {
        for (Option& o : m_options)
            if (const auto v = options->find(o.id); v != options->end() && o.id != kCpus)
                if (const int c = indexOf(o.choices, *v); c >= 0) o.value = c;
        if (const auto v = options->find(kCpus); v != options->end())
            if (const int c = indexOf(option(kCpus)->choices, *v); c >= 0) option(kCpus)->value = c;
        refreshCpuRows();
    }
    changed();
}

} // namespace kke
