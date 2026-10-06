#pragma once
// Minimal header-only JSON reader: null/bool/number/string/array/object, \uXXXX decoded as '?'.
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false, frac = false;  // frac: the number was written with a '.' or an exponent
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::vector<std::pair<std::string, Json>> obj;  // keys in file order

    // Missing key / index / wrong type => a Null value, so lookups chain safely.
    const Json &operator[](const char *k) const {
        for (auto &kv : obj) if (kv.first == k) return kv.second;
        return none();
    }
    const Json &operator[](int i) const { return i >= 0 && i < (int)arr.size() ? arr[i] : none(); }
    size_t size() const { return type == Arr ? arr.size() : obj.size(); }
    float f(float def = 0) const { return type == Num ? (float)num : def; }
    bool is(bool def = false) const { return type == Bool ? b : def; }
    std::string s(const std::string &def = "") const { return type == Str ? str : def; }

    // Returns false on syntax error or trailing garbage.
    static bool parse(const std::string &src, Json &out) {
        const char *p = src.c_str();
        return value(p, out, 0) && (ws(p), !*p);
    }

private:
    static const Json &none() { static const Json n; return n; }
    static void ws(const char *&p) { while (isspace((unsigned char)*p)) p++; }
    static bool string(const char *&p, std::string &out) {
        if (*p++ != '"') return false;
        for (out.clear(); *p != '"'; p++) {
            if (!*p) return false;
            if (*p != '\\') { out += *p; continue; }
            char c = *++p;
            if (c == 'u') { for (int i = 0; i < 4; i++) if (!isxdigit((unsigned char)*++p)) return false; c = '?'; }
            else if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c != '"' && c != '\\' && c != '/') return false;
            out += c;
        }
        p++;
        return true;
    }
    static bool value(const char *&p, Json &v, int depth) {
        ws(p);
        if (depth > 64) return false;
        if (*p == '"') return v.type = Str, string(p, v.str);
        if (!strncmp(p, "true", 4)) return p += 4, v.type = Bool, v.b = true;
        if (!strncmp(p, "false", 5)) return p += 5, v.type = Bool, true;
        if (!strncmp(p, "null", 4)) return p += 4, true;
        char open = *p, close = open == '[' ? ']' : '}';
        if (open != '[' && open != '{') {
            char *e;
            v.num = strtod(p, &e);
            if (e == p) return false;
            for (const char *q = p; q < e; q++) v.frac |= *q == '.' || *q == 'e' || *q == 'E';
            return p = e, v.type = Num, true;
        }
        v.type = open == '[' ? Arr : Obj;
        p++;
        if (ws(p), *p == close) return p++, true;
        for (;;) {
            Json item;
            std::string key;
            if (v.type == Obj && (ws(p), !string(p, key) || (ws(p), *p++ != ':'))) return false;
            if (!value(p, item, depth + 1)) return false;
            if (v.type == Arr) v.arr.push_back(std::move(item));
            else v.obj.emplace_back(std::move(key), std::move(item));
            ws(p);
            if (*p == close) return p++, true;
            if (*p++ != ',') return false;
        }
    }
};
