#include "config_diff.hpp"
#include <map>

using json = nlohmann::ordered_json;

static std::string Short(const json& v) {
    constexpr size_t kMax = 80;   // pixel samples are long base64 strings
    std::string s = v.is_null() ? "(none)" : v.dump();
    if (s.size() > kMax) s = s.substr(0, kMax) + "...(" + std::to_string(s.size()) + " chars)";
    return s;
}

// Array whose elements all carry an "id" (workflows, activity lists)
static bool IsIdList(const json& a) {
    if (!a.is_array()) return false;
    for (auto& e : a)
        if (!e.is_object() || !e.contains("id") || !e["id"].is_string()) return false;
    return true;
}

static std::string Label(const json& e, size_t index) {
    if (e.contains("name") && e.contains("activities"))            // workflow
        return "workflow \"" + e.value("name", std::string()) + "\"";
    std::string s = "#" + std::to_string(index + 1);
    if (e.contains("type")) s += " " + e.value("type", std::string());
    std::string id = e.value("id", std::string());
    if (!id.empty()) s += " [" + id.substr(0, 8) + "]";
    return s;
}

static std::string Join(const std::string& path, const std::string& part) {
    return path.empty() ? part : path + " > " + part;
}

static void Diff(const std::string& path, const json& a, const json& b,
                 std::vector<std::string>& out);

static void DiffIdList(const std::string& path, const json& a, const json& b,
                       std::vector<std::string>& out) {
    std::map<std::string, size_t> ia, ib;
    for (size_t i = 0; i < a.size(); ++i) ia[a[i]["id"].get<std::string>()] = i;
    for (size_t i = 0; i < b.size(); ++i) ib[b[i]["id"].get<std::string>()] = i;

    for (size_t i = 0; i < a.size(); ++i)
        if (!ib.count(a[i]["id"].get<std::string>()))
            out.push_back(Join(path, "removed " + Label(a[i], i)));
    for (size_t i = 0; i < b.size(); ++i)
        if (!ia.count(b[i]["id"].get<std::string>()))
            out.push_back(Join(path, "added " + Label(b[i], i)));

    // Order of the items present on both sides
    std::vector<std::string> oa, ob;
    for (auto& e : a) if (ib.count(e["id"].get<std::string>())) oa.push_back(e["id"]);
    for (auto& e : b) if (ia.count(e["id"].get<std::string>())) ob.push_back(e["id"]);
    if (oa != ob) out.push_back(Join(path, "reordered"));

    for (size_t i = 0; i < b.size(); ++i) {
        auto it = ia.find(b[i]["id"].get<std::string>());
        if (it == ia.end()) continue;
        const json& before = a[it->second];
        if (before == b[i]) continue;
        // Label with the old name so a rename is still recognisable
        Diff(Join(path, Label(before, i)), before, b[i], out);
    }
}

static void Diff(const std::string& path, const json& a, const json& b,
                 std::vector<std::string>& out) {
    if (a == b) return;
    if (a.is_object() && b.is_object()) {
        for (auto& [k, v] : a.items())
            Diff(Join(path, k), v, b.contains(k) ? b[k] : json(), out);
        for (auto& [k, v] : b.items())
            if (!a.contains(k)) Diff(Join(path, k), json(), v, out);
        return;
    }
    if (IsIdList(a) && IsIdList(b)) { DiffIdList(path, a, b, out); return; }
    if (a.is_array() && b.is_array() && a.size() == b.size()) {
        for (size_t i = 0; i < a.size(); ++i)
            Diff(Join(path, "[" + std::to_string(i + 1) + "]"), a[i], b[i], out);
        return;
    }
    out.push_back(path + ": " + Short(a) + " -> " + Short(b));
}

std::vector<std::string> DiffConfigJson(const json& before, const json& after) {
    std::vector<std::string> out;
    Diff("", before, after, out);
    const std::string kWf = "workflows > ";   // implied — workflows are labelled anyway
    for (auto& l : out)
        if (l.compare(0, kWf.size(), kWf) == 0) l.erase(0, kWf.size());
    return out;
}
