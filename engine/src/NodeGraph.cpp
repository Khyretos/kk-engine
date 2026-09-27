#include "kke/NodeGraph.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace kke {

// ---------------------------------------------------------------- library

const PinDef* NodeDef::input(const std::string& name) const {
    for (const PinDef& p : inputs)
        if (p.name == name) return &p;
    return nullptr;
}

const PinDef* NodeDef::output(const std::string& name) const {
    for (const PinDef& p : outputs)
        if (p.name == name) return &p;
    return nullptr;
}

namespace {

PinDef flowPin(std::string name = ">", std::string label = "") { return PinDef{ std::move(name), "flow", std::move(label), {} }; }
PinDef pin(const ApiParam& p) { return PinDef{ p.name, p.type, p.label.empty() ? p.name : p.label, p.fallback }; }
PinDef pin(std::string name, std::string type, std::string label, std::string fallback = {}) {
    return PinDef{ std::move(name), std::move(type), std::move(label), std::move(fallback) };
}

// Built-in nodes: flow and a few calculations. They compile to plain Lua
// (hook, timer, if, for, math), which is why they aren't bindings.
std::vector<NodeDef> builtinNodes() {
    using K = NodeDef::Kind;
    using namespace api_type;
    std::vector<NodeDef> n;
    auto def = [&](std::string type, std::string title, std::string category, K kind, std::string doc) -> NodeDef& {
        NodeDef d;
        d.type = std::move(type);
        d.title = std::move(title);
        d.category = std::move(category);
        d.kind = kind;
        d.doc = std::move(doc);
        n.push_back(std::move(d));
        return n.back();
    };
    {
        NodeDef& d = def("flow:start", "When the game starts", "When", K::Event, "Runs once when the game starts (and again when you change this graph)");
        d.outputs = { flowPin() };
    }
    {
        NodeDef& d = def("flow:every", "Every few seconds", "When", K::Event, "Runs again and again, this many seconds apart");
        d.inputs = { pin("seconds", Number, "Seconds", "2") };
        d.outputs = { flowPin() };
    }
    {
        NodeDef& d = def("flow:wait", "Wait", "Then", K::Action, "Waits this many seconds, then goes on");
        d.inputs = { flowPin(), pin("seconds", Number, "Seconds", "1") };
        d.outputs = { flowPin() };
    }
    {
        NodeDef& d = def("flow:if", "If", "Then", K::Flow, "Goes one way if the answer is yes, the other way if not");
        d.inputs = { flowPin(), pin("yes", Bool, "Is it so?", "true") };
        d.outputs = { flowPin("then", "Yes"), flowPin("else", "No") };
    }
    {
        NodeDef& d = def("flow:repeat", "Do it again", "Then", K::Flow, "Does the Each way this many times, then goes on Done");
        d.inputs = { flowPin(), pin("times", Number, "Times", "3") };
        d.outputs = { flowPin("each", "Each"), pin("count", Number, "Count"), flowPin("done", "Done") };
    }
    {
        NodeDef& d = def("value:me", "Me", "Values", K::Value, "The thing this graph belongs to");
        d.outputs = { pin("me", Thing, "Me") };
    }
    {
        NodeDef& d = def("value:random", "Random number", "Values", K::Value, "A whole number from Low to High");
        d.inputs = { pin("low", Number, "Low", "1"), pin("high", Number, "High", "6") };
        d.outputs = { pin("value", Number, "Number") };
    }
    {
        NodeDef& d = def("value:place", "Place", "Values", K::Value, "A place: across, up, along");
        d.inputs = { pin("x", Number, "Across", "0"), pin("y", Number, "Up", "0"), pin("z", Number, "Along", "0") };
        d.outputs = { pin("pos", Vec, "Place") };
    }
    {
        NodeDef& d = def("value:add", "Add", "Values", K::Value, "Adds two numbers, or moves a place by another");
        d.inputs = { pin("a", "any", "A", "0"), pin("b", "any", "B", "0") };
        d.outputs = { pin("sum", "any", "Sum") };
    }
    {
        NodeDef& d = def("value:same", "Is the same", "Values", K::Value, "Yes if A and B are the same");
        d.inputs = { pin("a", "any", "A", "nil"), pin("b", "any", "B", "nil") };
        d.outputs = { pin("yes", Bool, "Same?") };
    }
    {
        NodeDef& d = def("value:more", "Is more than", "Values", K::Value, "Yes if A is bigger than B");
        d.inputs = { pin("a", Number, "A", "0"), pin("b", Number, "B", "0") };
        d.outputs = { pin("yes", Bool, "More?") };
    }
    return n;
}

} // namespace

NodeLibrary NodeLibrary::fromApi(const std::vector<ApiFunction>& functions, const std::vector<ApiEvent>& events) {
    NodeLibrary lib;
    std::vector<NodeDef> builtins = builtinNodes();
    // "When" first: events are where every graph starts.
    for (NodeDef& d : builtins)
        if (d.kind == NodeDef::Kind::Event) lib.m_nodes.push_back(d);
    for (const ApiEvent& e : events) {
        NodeDef d;
        d.type = "event:" + e.name;
        d.title = e.label.empty() ? e.name : e.label;
        d.category = "When";
        d.doc = e.doc;
        d.kind = NodeDef::Kind::Event;
        d.event = e.name;
        d.subject = e.subject;
        d.outputs.push_back(flowPin());
        for (const ApiParam& f : e.fields) d.outputs.push_back(pin(f));
        lib.m_nodes.push_back(std::move(d));
    }
    for (NodeDef& d : builtins)
        if (d.kind != NodeDef::Kind::Event) lib.m_nodes.push_back(d);
    for (const ApiFunction& f : functions) {
        if (f.label.empty()) continue; // documented for Lua, no node (lists, ...)
        NodeDef d;
        d.type = "call:" + f.qualifiedName();
        d.title = f.label;
        d.category = f.category.empty() ? "More" : f.category;
        d.doc = f.doc;
        d.function = f.qualifiedName();
        d.kind = f.pure ? NodeDef::Kind::Value : NodeDef::Kind::Action;
        if (!f.pure) d.inputs.push_back(flowPin());
        for (const ApiParam& p : f.params) d.inputs.push_back(pin(p));
        if (!f.pure) d.outputs.push_back(flowPin());
        if (!f.result.type.empty()) d.outputs.push_back(pin(f.result.name.empty() ? std::string("result") : f.result.name, f.result.type,
                                                             f.result.label.empty() ? f.result.name : f.result.label));
        lib.m_nodes.push_back(std::move(d));
    }
    return lib;
}

const NodeDef* NodeLibrary::find(const std::string& type) const {
    for (const NodeDef& d : m_nodes)
        if (d.type == type) return &d;
    return nullptr;
}

std::vector<std::string> NodeLibrary::categories() const {
    std::vector<std::string> out;
    for (const NodeDef& d : m_nodes)
        if (std::find(out.begin(), out.end(), d.category) == out.end()) out.push_back(d.category);
    return out;
}

bool NodeLibrary::compatible(const std::string& from, const std::string& to) {
    if (from == "flow" || to == "flow") return from == to;
    if (from == to || from == "any" || to == "any") return true;
    // Words are words: a block id or a sound name is text too.
    auto word = [](const std::string& t) { return t == api_type::Text || t == api_type::Block || t == api_type::Sound; };
    if (word(from) && word(to)) return true;
    // Anything can be said.
    return to == api_type::Text;
}

// ---------------------------------------------------------------- graph

GraphNode* NodeGraph::find(int id) {
    for (GraphNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const GraphNode* NodeGraph::find(int id) const {
    for (const GraphNode& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

int NodeGraph::add(const std::string& type, glm::vec2 pos) {
    GraphNode n;
    n.id = nextId++;
    n.type = type;
    n.pos = pos;
    nodes.push_back(std::move(n));
    return nodes.back().id;
}

void NodeGraph::remove(int nodeId) {
    std::erase_if(nodes, [&](const GraphNode& n) { return n.id == nodeId; });
    std::erase_if(links, [&](const GraphLink& l) { return l.fromNode == nodeId || l.toNode == nodeId; });
}

const GraphLink* NodeGraph::linkInto(int node, const std::string& pin) const {
    for (const GraphLink& l : links)
        if (l.toNode == node && l.toPin == pin) return &l;
    return nullptr;
}

bool NodeGraph::connect(const NodeLibrary& library, const GraphLink& link) {
    if (link.fromNode == link.toNode) return false;
    const GraphNode* a = find(link.fromNode);
    const GraphNode* b = find(link.toNode);
    if (!a || !b) return false;
    const NodeDef* da = library.find(a->type);
    const NodeDef* db = library.find(b->type);
    if (!da || !db) return false;
    const PinDef* out = da->output(link.fromPin);
    const PinDef* in = db->input(link.toPin);
    if (!out || !in || !NodeLibrary::compatible(out->type, in->type)) return false;
    std::erase_if(links, [&](const GraphLink& l) {
        // Several ways may lead into one step (a flow input), but a value
        // input holds one value, and a flow output leads one way.
        return (!in->flow() && l.toNode == link.toNode && l.toPin == link.toPin) ||
               (out->flow() && l.fromNode == link.fromNode && l.fromPin == link.fromPin);
    });
    links.push_back(link);
    return true;
}

void NodeGraph::disconnect(const GraphLink& link) {
    std::erase(links, link);
}

std::string NodeGraph::toJson() const {
    nlohmann::ordered_json j;
    j["name"] = name;
    j["nodes"] = nlohmann::ordered_json::array();
    for (const GraphNode& n : nodes) {
        nlohmann::ordered_json o;
        o["id"] = n.id;
        o["type"] = n.type;
        o["pos"] = { std::round(n.pos.x), std::round(n.pos.y) };
        if (!n.values.empty()) o["values"] = n.values;
        j["nodes"].push_back(std::move(o));
    }
    j["links"] = nlohmann::ordered_json::array();
    for (const GraphLink& l : links) j["links"].push_back({ { "from", { l.fromNode, l.fromPin } }, { "to", { l.toNode, l.toPin } } });
    return j.dump(2);
}

NodeGraph NodeGraph::fromJson(const std::string& text, const std::string& sourceName) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error(sourceName + ": not JSON (" + e.what() + ")");
    }
    if (!j.is_object()) throw std::runtime_error(sourceName + ": a graph is a JSON object");
    NodeGraph g;
    try {
        g.name = j.value("name", std::string());
        std::set<int> ids;
        for (const nlohmann::json& o : j.value("nodes", nlohmann::json::array())) {
            GraphNode n;
            n.id = o.at("id").get<int>();
            n.type = o.at("type").get<std::string>();
            if (n.id <= 0 || !ids.insert(n.id).second) throw std::runtime_error("node id " + std::to_string(n.id) + " is not a new positive number");
            if (auto p = o.find("pos"); p != o.end() && p->is_array() && p->size() == 2) n.pos = glm::vec2((*p)[0].get<float>(), (*p)[1].get<float>());
            if (auto v = o.find("values"); v != o.end() && v->is_object())
                for (auto it = v->begin(); it != v->end(); ++it)
                    if (it->is_string()) n.values[it.key()] = it->get<std::string>();
            g.nextId = std::max(g.nextId, n.id + 1);
            g.nodes.push_back(std::move(n));
        }
        for (const nlohmann::json& o : j.value("links", nlohmann::json::array())) {
            const nlohmann::json& from = o.at("from");
            const nlohmann::json& to = o.at("to");
            GraphLink l{ from.at(0).get<int>(), from.at(1).get<std::string>(), to.at(0).get<int>(), to.at(1).get<std::string>() };
            if (!ids.count(l.fromNode) || !ids.count(l.toNode)) throw std::runtime_error("a link to a node that isn't there");
            g.links.push_back(std::move(l));
        }
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(sourceName + ": " + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error(sourceName + ": bad graph (" + e.what() + ")");
    }
    return g;
}

// ---------------------------------------------------------------- values

namespace {

bool parseNumber(const std::string& s, double& out) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    if (a == std::string::npos) return false;
    const char* first = s.data() + a;
    const char* last = s.data() + b + 1;
    if (*first == '+') ++first;
    auto r = std::from_chars(first, last, out);
    return r.ec == std::errc() && r.ptr == last && std::isfinite(out);
}

std::string luaNumber(double v) {
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof(buf), v);
    std::string s(buf, r.ptr);
    return s;
}

std::string luaString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 32 || c == 127) {
                char b[8];
                std::snprintf(b, sizeof(b), "\\%03u", unsigned(c));
                out += b;
            } else {
                out += char(c);
            }
        }
    }
    return out + "\"";
}

} // namespace

bool valueToLua(const std::string& type, const std::string& text, std::string& lua) {
    using namespace api_type;
    if (type == Number) {
        double v = 0.0;
        if (!parseNumber(text, v)) return false;
        lua = luaNumber(v);
        return true;
    }
    if (type == Bool) {
        if (text == "true" || text == "yes") lua = "true";
        else if (text == "false" || text == "no") lua = "false";
        else return false;
        return true;
    }
    if (type == Vec) {
        std::string t = text;
        std::replace(t.begin(), t.end(), ',', ' ');
        std::istringstream in(t);
        std::string part;
        std::vector<double> v;
        while (in >> part) {
            double d = 0.0;
            if (!parseNumber(part, d)) return false;
            v.push_back(d);
        }
        if (v.size() != 3) return false;
        lua = "Vec(" + luaNumber(v[0]) + ", " + luaNumber(v[1]) + ", " + luaNumber(v[2]) + ")";
        return true;
    }
    if (type == Thing) return false; // things are linked, not typed
    if (type == "any") {
        // A number if it reads as one, else words.
        double d = 0.0;
        lua = parseNumber(text, d) ? luaNumber(d) : luaString(text);
        return true;
    }
    lua = luaString(text); // text, block, sound
    return true;
}

// ---------------------------------------------------------------- compiling

bool CompiledGraph::ok() const {
    return std::none_of(issues.begin(), issues.end(), [](const GraphIssue& i) { return i.error; });
}

int CompiledGraph::nodeForError(const std::string& message, const std::string& source) const {
    // Lua reports "source:line: message" (chunk name "@source").
    const std::string key = source + ":";
    for (size_t at = message.find(key); at != std::string::npos; at = message.find(key, at + 1)) {
        size_t p = at + key.size();
        int line = 0;
        auto r = std::from_chars(message.data() + p, message.data() + message.size(), line);
        if (r.ec != std::errc() || r.ptr == message.data() + p) continue;
        if (line >= 1 && size_t(line) <= lineNode.size() && lineNode[size_t(line) - 1]) return lineNode[size_t(line) - 1];
    }
    return 0;
}

namespace {

class Compiler {
public:
    Compiler(const NodeGraph& g, const NodeLibrary& lib, const CompileOptions& opt) : m_g(g), m_lib(lib), m_opt(opt) {}

    CompiledGraph run() {
        line(0, "-- Made from the node graph \"" + sanitize(m_g.name) + "\": change the graph, not this text.");
        if (m_opt.owner) line(0, "local me = " + std::to_string(m_opt.owner));
        else line(0, "local me = nil");
        // Every node must be one the library knows.
        for (const GraphNode& n : m_g.nodes)
            if (!m_lib.find(n.type)) issue(n.id, true, "Unknown block '" + n.type + "' (made with a newer version?)");
        // Events in a stable order: top to bottom on the canvas.
        std::vector<const GraphNode*> events;
        for (const GraphNode& n : m_g.nodes) {
            const NodeDef* d = m_lib.find(n.type);
            if (d && d->kind == NodeDef::Kind::Event) events.push_back(&n);
        }
        std::stable_sort(events.begin(), events.end(), [](const GraphNode* a, const GraphNode* b) { return a->pos.y < b->pos.y; });
        std::vector<std::string> starts;
        for (const GraphNode* e : events) emitEvent(*e, starts);
        if (!starts.empty()) {
            // After every hook is in place, so a start chain that runs one
            // of the graph's own events already finds it.
            for (const std::string& s : starts) line(0, s + "()");
        }
        // Nodes that do something but that no "When" reaches.
        for (const GraphNode& n : m_g.nodes) {
            const NodeDef* d = m_lib.find(n.type);
            if (!d || d->kind == NodeDef::Kind::Event || d->kind == NodeDef::Kind::Value) continue;
            if (!m_reached.count(n.id)) issue(n.id, false, "Not connected to a \"When\" yet, so it never runs");
        }
        m_out.lua = m_text.str();
        return std::move(m_out);
    }

private:
    struct Scope {
        std::string eventVar;          // "e3": the event's table
        int eventNode = 0;
        std::set<int> done;            // action nodes whose results exist here
        std::map<int, std::string> vars;   // node -> Lua variable holding its result
        std::vector<int> path;         // flow nodes on the way here (loops)
    };

    static std::string sanitize(const std::string& s) {
        std::string o;
        for (char c : s) o += (c == '\n' || c == '\r') ? ' ' : c;
        return o;
    }

    void line(int node, const std::string& text) {
        m_text << text << "\n";
        m_out.lineNode.push_back(node);
    }
    std::string ind(int depth) const { return std::string(size_t(depth) * 2, ' '); }

    void issue(int node, bool error, std::string message) {
        for (const GraphIssue& i : m_out.issues)
            if (i.node == node && i.message == message) return;
        m_out.issues.push_back(GraphIssue{ node, error, std::move(message) });
    }

    // The trace call for a step, with a trailing space ("" when off).
    std::string tr(int node) const { return m_opt.trace.empty() ? std::string() : m_opt.trace + "(" + std::to_string(node) + ") "; }

    std::string hookName(int node) const { return luaString(m_opt.source + "#" + std::to_string(node)); }

    // The Lua expression for input `pin` of node `n` (def `d`).
    std::string input(const GraphNode& n, const PinDef& p, const Scope* scope, int depth = 0) {
        if (const GraphLink* l = m_g.linkInto(n.id, p.name)) return linked(*l, n, p, scope, depth);
        if (auto v = n.values.find(p.name); v != n.values.end() && !v->second.empty()) {
            std::string lua;
            if (valueToLua(p.type, v->second, lua)) return lua;
            issue(n.id, true, "\"" + v->second + "\" is not a " + (p.type == api_type::Vec ? std::string("place (three numbers)") : p.type) +
                                  " (" + p.label + ")");
            return "nil";
        }
        // A thing's own graph (or a person's recipe) acts on itself by default.
        if (p.type == api_type::Thing && (m_opt.owner || (!m_opt.block.empty() && !m_opt.toolBlock))) return "me";
        if (!p.fallback.empty()) return p.fallback;
        issue(n.id, true, p.label + " needs something connected");
        return "nil";
    }

    std::string linked(const GraphLink& l, const GraphNode& into, const PinDef& p, const Scope* scope, int depth) {
        const GraphNode* src = m_g.find(l.fromNode);
        const NodeDef* sd = src ? m_lib.find(src->type) : nullptr;
        const PinDef* out = sd ? sd->output(l.fromPin) : nullptr;
        if (!src || !sd || !out) {
            issue(into.id, true, p.label + " is connected to something that isn't there");
            return "nil";
        }
        if (!NodeLibrary::compatible(out->type, p.type)) {
            issue(into.id, true, p.label + " can't take a " + out->type);
            return "nil";
        }
        if (depth > 64) {
            issue(into.id, true, "Values go round in a circle");
            return "nil";
        }
        std::string expr;
        if (sd->kind == NodeDef::Kind::Value) {
            expr = valueExpr(*src, *sd, scope, depth + 1);
        } else if (sd->kind == NodeDef::Kind::Event) {
            if (!scope || scope->eventNode != src->id) {
                issue(into.id, true, p.label + " comes from a different \"When\"; connect it inside that one's chain");
                return "nil";
            }
            expr = scope->eventVar + "." + l.fromPin;
        } else {
            // An action's result, or a loop's count: only once it has run.
            const auto v = scope ? scope->vars.find(src->id) : std::map<int, std::string>::const_iterator{};
            if (!scope || v == scope->vars.end()) {
                issue(into.id, true, p.label + " uses \"" + sd->title + "\" before it has happened; connect that one earlier in the chain");
                return "nil";
            }
            expr = v->second;
        }
        // Text pins say anything: turn numbers and things into words.
        if (p.type == api_type::Text && out->type != api_type::Text && out->type != api_type::Block && out->type != api_type::Sound)
            expr = "tostring(" + expr + ")";
        return expr;
    }

    // A value node has one result, whichever output it is read from.
    std::string valueExpr(const GraphNode& n, const NodeDef& d, const Scope* scope, int depth) {
        auto in = [&](const char* name) {
            const PinDef* p = d.input(name);
            return p ? input(n, *p, scope, depth) : std::string("nil");
        };
        if (d.type == "value:me") return "me";
        if (d.type == "value:random") return "math.random(math.floor(" + in("low") + "), math.floor(" + in("high") + "))";
        if (d.type == "value:place") return "Vec(" + in("x") + ", " + in("y") + ", " + in("z") + ")";
        if (d.type == "value:add") return "(" + in("a") + " + " + in("b") + ")";
        if (d.type == "value:same") return "(" + in("a") + " == " + in("b") + ")";
        if (d.type == "value:more") return "(" + in("a") + " > " + in("b") + ")";
        if (!d.function.empty()) {
            return d.function + "(" + args(n, d, scope, depth) + ")";
        }
        issue(n.id, true, "Can't work out \"" + d.title + "\"");
        return "nil";
    }

    std::string args(const GraphNode& n, const NodeDef& d, const Scope* scope, int depth = 0) {
        std::string a;
        for (const PinDef& p : d.inputs) {
            if (p.flow()) continue;
            if (!a.empty()) a += ", ";
            a += input(n, p, scope, depth);
        }
        return a;
    }

    // What `me` is inside one of this graph's event handlers, and whether
    // the handler returns early for events about something else: the lines
    // that go first in the handler.
    std::vector<std::string> filter(const NodeDef& d, const std::string& ev) const {
        if (!d.subject.empty() && m_opt.owner) return { "if " + ev + "." + d.subject + " ~= me then return end" };
        if (!m_opt.block.empty() && m_opt.toolBlock) {
            for (const PinDef& p : d.outputs)
                if (p.name == "by") return { "if " + ev + ".by ~= " + luaString(m_opt.block) + " then return end" };
            return {};
        }
        if (!m_opt.block.empty() && !d.subject.empty())
            return { "local me = " + ev + "." + d.subject, "if play.blockOf(me) ~= " + luaString(m_opt.block) + " then return end" };
        return {};
    }

    void emitEvent(const GraphNode& n, std::vector<std::string>& starts) {
        const NodeDef& d = *m_lib.find(n.type);
        const std::string title = "-- " + d.title;
        Scope scope;
        scope.eventNode = n.id;
        scope.eventVar = "e" + std::to_string(n.id);
        m_reached.insert(n.id);
        if (d.type == "flow:start") {
            const std::string fn = "start" + std::to_string(n.id);
            starts.push_back(fn);
            line(n.id, "local function " + fn + "() " + tr(n.id) + title);
            emitChain(n, ">", scope, 1);
            line(n.id, "end");
        } else if (d.type == "flow:every") {
            const std::string secs = input(n, *d.input("seconds"), &scope);
            line(n.id, "timer.Create(" + hookName(n.id) + ", math.max(0.1, " + secs + "), 0, function() " + tr(n.id) + title);
            emitChain(n, ">", scope, 1);
            line(n.id, "end)");
        } else {
            // Lit up after the filter: only when it's really this graph's event.
            const std::vector<std::string> f = filter(d, scope.eventVar);
            line(n.id, "hook.Add(" + luaString(d.event) + ", " + hookName(n.id) + ", function(" + scope.eventVar + ") " +
                           (f.empty() ? tr(n.id) : std::string()) + title);
            for (size_t i = 0; i < f.size(); ++i) line(n.id, ind(1) + f[i] + (i + 1 == f.size() && !tr(n.id).empty() ? " " + tr(n.id) : std::string()));
            emitChain(n, ">", scope, 1);
            line(n.id, "end)");
        }
    }

    // Emits what hangs off flow output `pin` of node `from`.
    void emitChain(const GraphNode& from, const std::string& pin, Scope scope, int depth) {
        std::vector<const GraphNode*> next;
        for (const GraphLink& l : m_g.links) {
            if (l.fromNode != from.id || l.fromPin != pin) continue;
            const GraphNode* t = m_g.find(l.toNode);
            const NodeDef* td = t ? m_lib.find(t->type) : nullptr;
            if (!t || !td) continue;
            const PinDef* in = td->input(l.toPin);
            if (in && in->flow()) next.push_back(t);
        }
        std::stable_sort(next.begin(), next.end(), [](const GraphNode* a, const GraphNode* b) { return a->pos.y < b->pos.y; });
        for (const GraphNode* t : next) emitNode(*t, scope, depth);
    }

    void emitNode(const GraphNode& n, Scope& scope, int depth) {
        const NodeDef& d = *m_lib.find(n.type);
        if (std::find(scope.path.begin(), scope.path.end(), n.id) != scope.path.end()) {
            issue(n.id, true, "The arrows go round in a circle here; that would never stop");
            return;
        }
        if (depth > 32) {
            issue(n.id, true, "Too many steps inside each other");
            return;
        }
        m_reached.insert(n.id);
        scope.path.push_back(n.id);
        const std::string title = " -- " + d.title;
        const std::string lead = ind(depth);
        const std::string pad = lead + tr(n.id); // the step's first line: lights it up
        if (d.type == "flow:wait") {
            line(n.id, pad + "timer.Simple(math.max(0, " + input(n, *d.input("seconds"), &scope) + "), function()" + title);
            emitChain(n, ">", scope, depth + 1);
            line(n.id, lead + "end)");
            return; // what comes after runs later, inside the timer
        }
        if (d.type == "flow:if") {
            line(n.id, pad + "if " + input(n, *d.input("yes"), &scope) + " then" + title);
            emitChain(n, "then", scope, depth + 1);
            line(n.id, lead + "else");
            emitChain(n, "else", scope, depth + 1);
            line(n.id, lead + "end");
            return;
        }
        if (d.type == "flow:repeat") {
            const std::string var = "i" + std::to_string(n.id);
            line(n.id, pad + "for " + var + " = 1, math.min(1000, math.floor(" + input(n, *d.input("times"), &scope) + ")) do" + title);
            Scope inner = scope;
            inner.vars[n.id] = var;
            emitChain(n, "each", inner, depth + 1);
            line(n.id, lead + "end");
            emitChain(n, "done", scope, depth);
            return;
        }
        if (!d.function.empty()) {
            const std::string call = d.function + "(" + args(n, d, &scope) + ")";
            bool hasResult = false;
            for (const PinDef& p : d.outputs) hasResult = hasResult || !p.flow();
            if (hasResult) {
                const std::string var = "n" + std::to_string(n.id);
                line(n.id, pad + "local " + var + " = " + call + title);
                scope.vars[n.id] = var;
            } else {
                line(n.id, pad + call + title);
            }
            emitChain(n, ">", scope, depth);
            return;
        }
        issue(n.id, true, "\"" + d.title + "\" can't be used in a chain");
    }

    const NodeGraph& m_g;
    const NodeLibrary& m_lib;
    const CompileOptions& m_opt;
    CompiledGraph m_out;
    std::ostringstream m_text;
    std::set<int> m_reached;
};

} // namespace

CompiledGraph compileGraph(const NodeGraph& graph, const NodeLibrary& library, const CompileOptions& options) {
    return Compiler(graph, library, options).run();
}

// ---------------------------------------------------------------- recipes

NodeGraph playBlockRecipe(const std::string& blockId) {
    NodeGraph g;
    if (blockId == "bat") {
        g.name = "Bat";
        const int hit = g.add("event:Hit", { 0.0f, 0.0f });
        const int knock = g.add("call:play.ragdoll", { 320.0f, 0.0f });
        const int bonk = g.add("call:play.sound", { 640.0f, 0.0f });
        g.find(bonk)->values["name"] = "wood";
        g.links = {
            { hit, ">", knock, ">" },
            { hit, "target", knock, "thing" },
            { hit, "push", knock, "push" },
            { knock, ">", bonk, ">" },
            { hit, "point", bonk, "pos" },
        };
    }
    return g;
}

} // namespace kke
