
// Hospital Management System - C++ backend
// A dependency-free REST API + static file server (POSIX sockets, C++17).
//
// Resources : patients, doctors, appointments, bills
// Endpoints : GET/POST /api/<resource>, GET/PUT/DELETE /api/<resource>/<id>, GET /api/stats
// Storage   : tab-separated files in the data directory (one file per resource)
//
// Usage: hms [port] [frontend_dir] [data_dir]
// Builds on Windows (MinGW-w64 or MSVC, Winsock) and on Linux/macOS (POSIX sockets).

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef SOCKET sock_t;
#define CLOSE_SOCKET closesocket
#define MAKE_DIR(p) _mkdir(p)
#else
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSE_SOCKET close
#define MAKE_DIR(p) mkdir(p, 0755)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using namespace std;
typedef map<string, string> Rec;

// ---------------------------------------------------------------- data model

struct Field {
    string name;
    char type;      // 's' string, 'n' number, 'b' boolean
    bool required;
    string def;     // default value ("@today" = current date)
};

struct Entity {
    string name;
    vector<Field> fields;
    vector<Rec> rows;
    int nextId = 1;
};

static vector<Entity> db;
static mutex dbMutex;
static string dataDir = "data";
static string webRoot = "frontend";

static Entity* findEntity(const string& n) {
    for (auto& e : db) if (e.name == n) return &e;
    return nullptr;
}

static Rec* findRow(Entity& e, int id) {
    for (auto& r : e.rows) if (atoi(r["id"].c_str()) == id) return &r;
    return nullptr;
}

// ------------------------------------------------------------------- helpers

static string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static string today() {
    time_t t = time(nullptr);
    char b[16];
    strftime(b, sizeof b, "%Y-%m-%d", localtime(&t));
    return b;
}

static string jsonEscape(const string& s) {
    string o;
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o;
}

static string cleanForStorage(string s) {
    for (char& c : s) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return s;
}

static string toJson(Entity& e, Rec& r) {
    string o = "{\"id\":" + r["id"];
    for (auto& f : e.fields) {
        const string& v = r[f.name];
        o += ",\"" + f.name + "\":";
        if (f.type == 'n') o += v.empty() ? "0" : v;
        else if (f.type == 'b') o += (v == "true") ? "true" : "false";
        else o += "\"" + jsonEscape(v) + "\"";
    }
    return o + "}";
}

// Minimal parser for a flat JSON object: {"key": "string" | number | true | false | null, ...}
static bool parseJson(const string& s, Rec& out) {
    size_t i = 0;
    auto ws = [&] { while (i < s.size() && isspace((unsigned char)s[i])) i++; };
    auto str = [&](string& r) -> bool {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { r += c; continue; }
            if (i >= s.size()) return false;
            char n = s[i++];
            switch (n) {
                case 'n': r += '\n'; break;
                case 't': r += '\t'; break;
                case 'r': r += '\r'; break;
                case 'b': r += '\b'; break;
                case 'f': r += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                    i += 4;
                    if (cp < 0x80) r += (char)cp;
                    else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
                    else { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: r += n;
            }
        }
        if (i >= s.size()) return false;
        i++;
        return true;
    };
    ws();
    if (i >= s.size() || s[i] != '{') return false;
    i++; ws();
    if (i < s.size() && s[i] == '}') return true;
    while (true) {
        ws();
        string k, v;
        if (!str(k)) return false;
        ws();
        if (i >= s.size() || s[i] != ':') return false;
        i++; ws();
        if (i < s.size() && s[i] == '"') {
            if (!str(v)) return false;
        } else {
            size_t st = i;
            while (i < s.size() && s[i] != ',' && s[i] != '}' && !isspace((unsigned char)s[i])) i++;
            v = s.substr(st, i - st);
            if (v == "null") v = "";
            else if (v.empty()) return false;
        }
        out[k] = v;
        ws();
        if (i >= s.size()) return false;
        if (s[i] == ',') { i++; continue; }
        return s[i] == '}';
    }
}

// --------------------------------------------------------------- persistence

static void save(Entity& e) {
    ofstream f(dataDir + "/" + e.name + ".tsv", ios::trunc | ios::binary);  // binary: keep "\n" on Windows
    for (auto& r : e.rows) {
        f << r["id"];
        for (auto& fl : e.fields) f << '\t' << cleanForStorage(r[fl.name]);
        f << '\n';
    }
}

static void load(Entity& e) {
    ifstream f(dataDir + "/" + e.name + ".tsv");
    string line;
    while (getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        vector<string> p;
        size_t start = 0, pos;
        while ((pos = line.find('\t', start)) != string::npos) {
            p.push_back(line.substr(start, pos - start));
            start = pos + 1;
        }
        p.push_back(line.substr(start));
        Rec r;
        r["id"] = p[0];
        for (size_t i = 0; i < e.fields.size(); i++)
            r[e.fields[i].name] = i + 1 < p.size() ? p[i + 1] : "";
        e.rows.push_back(r);
        e.nextId = max(e.nextId, atoi(p[0].c_str()) + 1);
    }
}

// ---------------------------------------------------------------- validation

// Merge `in` into `rec`, apply defaults, validate types. Returns "" or an error message.
static string applyInput(Entity& e, Rec& rec, const Rec& in) {
    for (auto& f : e.fields) {
        auto it = in.find(f.name);
        if (it != in.end()) rec[f.name] = trim(it->second);
        string& v = rec[f.name];
        if (v.empty() && !f.def.empty()) v = (f.def == "@today") ? today() : f.def;
        if (v.empty()) {
            if (f.required) return "Field '" + f.name + "' is required";
            if (f.type == 'n') v = "0";
            continue;
        }
        if (f.type == 'n') {
            int dots = 0;
            bool ok = true;
            for (char c : v) {
                if (c == '.') dots++;
                else if (!isdigit((unsigned char)c)) ok = false;
            }
            if (!ok || dots > 1 || v == "." || v.size() > 12)
                return "Field '" + f.name + "' must be a non-negative number";
            double d = strtod(v.c_str(), nullptr);
            char buf[32];
            if (d == floor(d)) snprintf(buf, sizeof buf, "%.0f", d);
            else snprintf(buf, sizeof buf, "%.2f", d);
            v = buf;
        } else if (f.type == 'b') {
            if (v != "true" && v != "false") return "Field '" + f.name + "' must be true or false";
        }
    }
    return "";
}

// Business rules. selfId is the record being updated (0 for create).
static string checkRules(Entity& e, Rec& r, int selfId) {
    if (e.name == "patients") {
        if (atoi(r["age"].c_str()) > 150) return "Age must be 150 or less";
    } else if (e.name == "doctors") {
        // nothing extra
    } else if (e.name == "bills") {
        if (!findRow(*findEntity("patients"), atoi(r["patientId"].c_str()))) return "Patient not found";
    } else if (e.name == "appointments") {
        const string& st = r["status"];
        if (st != "Scheduled" && st != "Completed" && st != "Cancelled")
            return "Status must be Scheduled, Completed or Cancelled";
        if (!findRow(*findEntity("patients"), atoi(r["patientId"].c_str()))) return "Patient not found";
        Rec* doc = findRow(*findEntity("doctors"), atoi(r["doctorId"].c_str()));
        if (!doc) return "Doctor not found";
        if (st == "Scheduled") {
            if ((*doc)["available"] != "true") return "Doctor is not available for appointments";
            for (auto& o : e.rows) {
                if (atoi(o["id"].c_str()) == selfId) continue;
                if (o["status"] == "Scheduled" && o["doctorId"] == r["doctorId"] &&
                    o["date"] == r["date"] && o["time"] == r["time"])
                    return "Doctor already has an appointment at that date and time";
            }
        }
    }
    return "";
}

// ---------------------------------------------------------------------- API

static void handleApi(const string& method, const vector<string>& seg, const string& body,
                      int& code, string& out) {
    lock_guard<mutex> lk(dbMutex);
    auto err = [&](int c, const string& m) {
        code = c;
        out = "{\"error\":\"" + jsonEscape(m) + "\"}";
    };

    if (seg.size() < 2) return err(404, "Not found");

    if (seg[1] == "stats" && method == "GET") {
        Entity& pt = *findEntity("patients");
        Entity& dr = *findEntity("doctors");
        Entity& ap = *findEntity("appointments");
        Entity& bl = *findEntity("bills");
        int avail = 0, todayCnt = 0, upcoming = 0;
        double revenue = 0, pending = 0;
        string t = today();
        for (auto& d : dr.rows) if (d["available"] == "true") avail++;
        for (auto& a : ap.rows) {
            if (a["status"] != "Scheduled") continue;
            if (a["date"] == t) todayCnt++;
            if (a["date"] >= t) upcoming++;
        }
        for (auto& b : bl.rows) {
            double amt = strtod(b["amount"].c_str(), nullptr);
            if (b["paid"] == "true") revenue += amt; else pending += amt;
        }
        char buf[512];
        snprintf(buf, sizeof buf,
                 "{\"patients\":%zu,\"doctors\":%zu,\"availableDoctors\":%d,\"appointments\":%zu,"
                 "\"scheduledToday\":%d,\"upcoming\":%d,\"revenue\":%.2f,\"pending\":%.2f}",
                 pt.rows.size(), dr.rows.size(), avail, ap.rows.size(), todayCnt, upcoming, revenue, pending);
        code = 200;
        out = buf;
        return;
    }

    Entity* e = findEntity(seg[1]);
    if (!e) return err(404, "Unknown resource");

    int id = 0;
    if (seg.size() >= 3) {
        id = atoi(seg[2].c_str());
        if (id <= 0) return err(400, "Invalid id");
    }
    if (seg.size() > 3) return err(404, "Not found");

    if (method == "GET" && seg.size() == 2) {
        out = "[";
        for (size_t i = 0; i < e->rows.size(); i++) {
            if (i) out += ",";
            out += toJson(*e, e->rows[i]);
        }
        out += "]";
        code = 200;
        return;
    }

    if (method == "POST" && seg.size() == 2) {
        Rec in, rec;
        if (!parseJson(body, in)) return err(400, "Invalid JSON body");
        string m = applyInput(*e, rec, in);
        if (m.empty()) m = checkRules(*e, rec, 0);
        if (!m.empty()) return err(m.find("already has") != string::npos ? 409 : 400, m);
        rec["id"] = to_string(e->nextId++);
        e->rows.push_back(rec);
        save(*e);
        code = 201;
        out = toJson(*e, e->rows.back());
        return;
    }

    if (seg.size() == 3) {
        Rec* row = findRow(*e, id);
        if (!row) return err(404, e->name + " record not found");

        if (method == "GET") { code = 200; out = toJson(*e, *row); return; }

        if (method == "PUT") {
            Rec in;
            if (!parseJson(body, in)) return err(400, "Invalid JSON body");
            Rec copy = *row;
            string m = applyInput(*e, copy, in);
            if (m.empty()) m = checkRules(*e, copy, id);
            if (!m.empty()) return err(m.find("already has") != string::npos ? 409 : 400, m);
            *row = copy;
            save(*e);
            code = 200;
            out = toJson(*e, *row);
            return;
        }

        if (method == "DELETE") {
            if (e->name == "doctors") {
                for (auto& a : findEntity("appointments")->rows)
                    if (atoi(a["doctorId"].c_str()) == id)
                        return err(409, "Doctor has appointment records. Mark the doctor unavailable instead.");
            }
            if (e->name == "patients") {  // cascade: remove the patient's appointments and bills
                for (const char* n : {"appointments", "bills"}) {
                    Entity& c = *findEntity(n);
                    c.rows.erase(remove_if(c.rows.begin(), c.rows.end(),
                                           [&](Rec& r) { return atoi(r["patientId"].c_str()) == id; }),
                                 c.rows.end());
                    save(c);
                }
            }
            e->rows.erase(remove_if(e->rows.begin(), e->rows.end(),
                                    [&](Rec& r) { return atoi(r["id"].c_str()) == id; }),
                          e->rows.end());
            save(*e);
            code = 200;
            out = "{\"deleted\":true}";
            return;
        }
    }
    err(405, "Method not allowed");
}

// -------------------------------------------------------------- HTTP server

static void sendResp(sock_t fd, int code, const string& type, const string& body) {
    static const map<int, string> status = {
        {200, "OK"}, {201, "Created"}, {204, "No Content"}, {400, "Bad Request"},
        {404, "Not Found"}, {405, "Method Not Allowed"}, {409, "Conflict"}, {500, "Internal Server Error"}};
    auto it = status.find(code);
    ostringstream o;
    o << "HTTP/1.1 " << code << " " << (it != status.end() ? it->second : "OK") << "\r\n"
      << "Content-Type: " << type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
      << "Access-Control-Allow-Headers: Content-Type\r\n\r\n";
    string msg = o.str() + body;
    size_t sent = 0;
    while (sent < msg.size()) {
        int n = (int)send(fd, msg.data() + sent, (int)(msg.size() - sent), MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

static string mimeType(const string& p) {
    auto ends = [&](const char* x) {
        size_t n = strlen(x);
        return p.size() >= n && p.compare(p.size() - n, n, x) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".js")) return "application/javascript; charset=utf-8";
    if (ends(".json")) return "application/json";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    if (ends(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static void serveStatic(sock_t fd, string path) {
    if (path == "/") path = "/index.html";
    if (path.find("..") != string::npos) return sendResp(fd, 400, "text/plain", "Bad path");
    ifstream f(webRoot + path, ios::binary);
    if (!f) return sendResp(fd, 404, "text/plain", "404 Not Found");
    stringstream ss;
    ss << f.rdbuf();
    sendResp(fd, 200, mimeType(path), ss.str());
}

static void handleConnection(sock_t fd) {
    string buf;
    char tmp[4096];
    size_t hdrEnd;
    while ((hdrEnd = buf.find("\r\n\r\n")) == string::npos) {
        int n = (int)recv(fd, tmp, (int)sizeof tmp, 0);
        if (n <= 0 || buf.size() > (1u << 20)) { CLOSE_SOCKET(fd); return; }
        buf.append(tmp, (size_t)n);
    }

    istringstream head(buf.substr(0, hdrEnd));
    string method, target, version, line;
    head >> method >> target >> version;
    size_t contentLength = 0;
    getline(head, line);
    while (getline(head, line)) {
        string low = line;
        transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)tolower(c); });
        if (low.rfind("content-length:", 0) == 0) contentLength = (size_t)atol(low.c_str() + 15);
    }
    if (contentLength > (1u << 20)) { sendResp(fd, 400, "text/plain", "Body too large"); CLOSE_SOCKET(fd); return; }

    string body = buf.substr(hdrEnd + 4);
    while (body.size() < contentLength) {
        int n = (int)recv(fd, tmp, (int)sizeof tmp, 0);
        if (n <= 0) break;
        body.append(tmp, (size_t)n);
    }
    body.resize(min(body.size(), contentLength));

    string path = target.substr(0, target.find('?'));
    try {
        if (method == "OPTIONS") {
            sendResp(fd, 204, "text/plain", "");
        } else if (path.rfind("/api", 0) == 0) {
            vector<string> seg;
            stringstream ps(path);
            string part;
            while (getline(ps, part, '/')) if (!part.empty()) seg.push_back(part);
            int code = 404;
            string out = "{\"error\":\"Not found\"}";
            handleApi(method, seg, body, code, out);
            sendResp(fd, code, "application/json; charset=utf-8", out);
        } else if (method == "GET") {
            serveStatic(fd, path);
        } else {
            sendResp(fd, 405, "text/plain", "Method not allowed");
        }
    } catch (const exception& ex) {
        sendResp(fd, 500, "application/json", string("{\"error\":\"") + jsonEscape(ex.what()) + "\"}");
    }
    CLOSE_SOCKET(fd);
}

// --------------------------------------------------------------------- main

static void seedIfEmpty() {
    Entity& d = *findEntity("doctors");
    if (!d.rows.empty() || !findEntity("patients")->rows.empty()) return;
    const char* sample[][4] = {
        {"Anita Sharma", "Cardiology", "9876500001", "true"},
        {"Rajesh Verma", "Orthopedics", "9876500002", "true"},
        {"Meera Kapoor", "Pediatrics", "9876500003", "true"}};
    for (auto& s : sample) {
        Rec r;
        r["id"] = to_string(d.nextId++);
        r["name"] = s[0]; r["specialization"] = s[1]; r["phone"] = s[2]; r["available"] = s[3];
        d.rows.push_back(r);
    }
    save(d);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { cerr << "Winsock initialisation failed" << endl; return 1; }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    if (argc > 2) webRoot = argv[2];
    if (argc > 3) dataDir = argv[3];
    MAKE_DIR(dataDir.c_str());

    db.push_back({"patients",
                  {{"name", 's', true, ""}, {"age", 'n', true, ""}, {"gender", 's', true, ""},
                   {"phone", 's', true, ""}, {"address", 's', false, ""}, {"disease", 's', false, ""}},
                  {}, 1});
    db.push_back({"doctors",
                  {{"name", 's', true, ""}, {"specialization", 's', true, ""},
                   {"phone", 's', false, ""}, {"available", 'b', false, "true"}},
                  {}, 1});
    db.push_back({"appointments",
                  {{"patientId", 'n', true, ""}, {"doctorId", 'n', true, ""}, {"date", 's', true, ""},
                   {"time", 's', true, ""}, {"status", 's', false, "Scheduled"}, {"notes", 's', false, ""}},
                  {}, 1});
    db.push_back({"bills",
                  {{"patientId", 'n', true, ""}, {"description", 's', true, ""}, {"amount", 'n', true, ""},
                   {"paid", 'b', false, "false"}, {"date", 's', false, "@today"}},
                  {}, 1});
    for (auto& e : db) load(e);
    seedIfEmpty();

    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { cerr << "Could not create socket" << endl; return 1; }
#ifndef _WIN32
    int yes = 1;  // on Windows, SO_REUSEADDR would allow port hijacking, so it is not set
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, (int)sizeof yes);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(srv, (sockaddr*)&addr, (int)sizeof addr) < 0) {
        cerr << "Could not bind to port " << port << " (is another program using it? try: hms.exe 9090)" << endl;
        return 1;
    }
    if (listen(srv, 64) < 0) { cerr << "listen() failed" << endl; return 1; }

    cout << "Hospital Management System running at http://localhost:" << port << "\n"
         << "  frontend: " << webRoot << "\n  data:     " << dataDir << endl;

    while (true) {
        sock_t fd = accept(srv, nullptr, nullptr);
        if (fd == INVALID_SOCKET) continue;
#ifdef _WIN32
        DWORD tv = 10000;  // milliseconds on Windows
#else
        timeval tv{10, 0};
#endif
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, (int)sizeof tv);
        thread(handleConnection, fd).detach();
    }
}
