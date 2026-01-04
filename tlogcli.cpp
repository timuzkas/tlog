#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std;

const string R = "\033[0m";
const string RED = "\033[31m";
const string GRN = "\033[32m";
const string YEL = "\033[33m";
const string CYN = "\033[36m";
const string DIM = "\033[90m";

struct Entry {
    uint64_t ts;
    string tid, sid, tags, msg;
    int lvl;
    string raw_line;
};

uint64_t parse_hex(const string& s) {
    try { return stoull(s, nullptr, 16); } catch(...) { return 0; }
}

string format_time(uint64_t ns) {
    time_t s = ns / 1000000000;
    tm* t = localtime(&s);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", t);
    return string(buf);
}

Entry parse_line(const string& line) {
    Entry e;
    e.raw_line = line;
    if (line.length() < 30) return e;

    size_t s1 = line.find(' ');
    size_t s2 = line.find(' ', s1 + 1);
    size_t s3 = line.find(' ', s2 + 1);
    size_t s4 = line.find(' ', s3 + 1);

    if (s1 == string::npos || s4 == string::npos) return e;

    e.ts = parse_hex(line.substr(0, s1));
    e.tid = line.substr(s1 + 1, s2 - s1 - 1);
    e.sid = line.substr(s2 + 1, s3 - s2 - 1);
    try { e.lvl = stoi(line.substr(s3 + 1, s4 - s3 - 1)); } catch(...) { e.lvl = 0; }

    size_t msg_start = s4 + 1;
    if (msg_start < line.size() && line[msg_start] == '[') {
        size_t bracket_end = line.find(']', msg_start);
        if (bracket_end != string::npos) {
            e.tags = line.substr(msg_start + 1, bracket_end - msg_start - 1);
            msg_start = bracket_end + 2;
        }
    }

    if (msg_start < line.size()) e.msg = line.substr(msg_start);
    return e;
}

void run_scan(const string& path, const string& filter) {
    ifstream file(path);
    string line;
    while (getline(file, line)) {
        Entry e = parse_line(line);
        if (e.tid.empty()) continue;
        if (!filter.empty() && line.find(filter) == string::npos && e.tags.find(filter) == string::npos) continue;

        string c = R, l = "INFO";
        if (e.lvl == 1) { c = YEL; l = "WARN"; }
        else if (e.lvl == 2) { c = RED; l = "FAIL"; }
        else if (e.lvl == 3) { c = DIM; l = "DBUG"; }

        cout << DIM << format_time(e.ts) << R << " " << CYN << e.tid.substr(0, 8) << R << " " << c << l << R << " " 
             << (e.tags.empty() ? "" : DIM + "[" + e.tags + "] " + R) << e.msg << "\n";
    }
}

void run_stats(const string& path) {
    ifstream file(path);
    string line;
    map<int, int> lvls;
    size_t count = 0, errors = 0;
    map<string, uint64_t> starts, ends;

    while (getline(file, line)) {
        Entry e = parse_line(line);
        if (e.tid.empty()) continue;
        count++;
        lvls[e.lvl]++;
        if (e.lvl == 2) errors++;
        if (starts.find(e.tid) == starts.end()) starts[e.tid] = e.ts;
        ends[e.tid] = e.ts;
    }

    vector<double> durs;
    for (auto const& [tid, start] : starts) durs.push_back((ends[tid] - start) / 1e6);
    sort(durs.begin(), durs.end());

    cout << "\n" << string(40, '-') << "\n";
    cout << "Total Events:   " << count << "\n";
    cout << "Unique Traces:  " << starts.size() << "\n";
    cout << "Error Traces:   " << (errors > 0 ? RED : GRN) << errors << R << "\n";
    cout << "Latency p50:    " << (durs.empty() ? 0 : durs[durs.size()*0.5]) << "ms\n";
    cout << "Latency p95:    " << YEL << (durs.empty() ? 0 : durs[durs.size()*0.95]) << R << "ms\n";
    cout << "Latency p99:    " << RED << (durs.empty() ? 0 : durs[durs.size()*0.99]) << R << "ms\n";
    cout << string(40, '-') << "\n";
}

void run_trace(const string& path, const string& query) {
    ifstream file(path);
    string line, target_tid = query;
    bool found = false;

    if (query.length() < 16) {
        while (getline(file, line)) {
            if (line.find(query) != string::npos) {
                target_tid = parse_line(line).tid;
                found = true;
                break;
            }
        }
        if (!found) return;
        file.clear();
        file.seekg(0);
    }

    uint64_t start = 0;
    int indent = 0;
    while (getline(file, line)) {
        Entry e = parse_line(line);
        if (e.tid != target_tid) continue;
        if (start == 0) start = e.ts;

        bool is_s = (e.msg.find("> ") == 0), is_e = (e.msg.find("< ") == 0);
        if (is_e && indent > 0) indent--;

        cout << fixed << setprecision(2) << setw(8) << (e.ts - start)/1e6 << "ms | ";
        for (int i = 0; i < indent; i++) cout << DIM << "│  " << R;
        
        if (is_s) cout << GRN << "┌ " << e.msg.substr(2) << R;
        else if (is_e) cout << DIM << "└ " << e.msg.substr(2) << R;
        else if (e.lvl == 2) cout << RED << "× " << e.msg << R;
        else cout << e.msg;
        
        if (!e.tags.empty()) cout << DIM << " [" << e.tags << "]" << R;
        cout << "\n";
        if (is_s) indent++;
    }
}

void run_tail(const string& path) {
    ifstream file(path);
    file.seekg(0, ios::end);
    string line;
    while (true) {
        while (getline(file, line)) {
            Entry e = parse_line(line);
            if (!e.tid.empty()) cout << (e.lvl == 2 ? RED : GRN) << e.tid.substr(0, 8) << R << " " << e.msg << "\n";
        }
        file.clear();
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void run_json(const string& path) {
    ifstream file(path);
    string line;
    cout << "[\n";
    bool first = true;
    while (getline(file, line)) {
        Entry e = parse_line(line);
        if (e.tid.empty()) continue;
        if (!first) cout << ",\n";
        cout << "  {\"ts\":" << e.ts << ",\"tid\":\"" << e.tid << "\",\"lvl\":" << e.lvl << ",\"msg\":\"" << e.msg << "\",\"tags\":\"" << e.tags << "\"}";
        first = false;
    }
    cout << "\n]\n";
}

void run_diff(const string& path, const string& id1, const string& id2) {
    ifstream file(path);
    string line;
    vector<Entry> t1, t2;
    while (getline(file, line)) {
        Entry e = parse_line(line);
        if (e.tid == id1) t1.push_back(e);
        if (e.tid == id2) t2.push_back(e);
    }
    for(size_t i = 0; i < max(t1.size(), t2.size()); ++i) {
        string l = (i < t1.size()) ? t1[i].msg : "", r = (i < t2.size()) ? t2[i].msg : "";
        cout << (l != r ? RED : GRN) << setw(45) << l.substr(0, 43) << "| " << r.substr(0, 43) << R << "\n";
    }
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    string cmd = argv[1], path = argv[2];
    if (cmd == "scan") run_scan(path, (argc > 3 ? argv[3] : ""));
    else if (cmd == "trace" && argc > 3) run_trace(path, argv[3]);
    else if (cmd == "stats") run_stats(path);
    else if (cmd == "tail") run_tail(path);
    else if (cmd == "json") run_json(path);
    else if (cmd == "diff" && argc > 4) run_diff(path, argv[3], argv[4]);
    return 0;
}
