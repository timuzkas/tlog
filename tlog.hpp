#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <thread>

namespace tlog {

enum Level : uint8_t { INFO = 0, WARN = 1, ERR = 2, DBUG = 3 };

struct Context {
    uint64_t trace_id = 0;
    uint64_t span_id = 0;
    char tags[128] = {0};
    int tags_len = 0;
    bool sample = true;
};

inline thread_local Context ctx = {};

inline uint64_t gen_id() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    return rng();
}

class RingBuffer {
    static constexpr size_t CAPACITY = 8192;
    static constexpr size_t LINE_SIZE = 512;
    
    char data[CAPACITY][LINE_SIZE];
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};

public:
    bool push(const char* line, size_t len) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t next = (h + 1) % CAPACITY;
        if (next == tail.load(std::memory_order_acquire)) return false;
        
        size_t copy_len = len < LINE_SIZE - 1 ? len : LINE_SIZE - 1;
        std::memcpy(data[h], line, copy_len);
        data[h][copy_len] = '\0';
        
        head.store(next, std::memory_order_release);
        return true;
    }
    
    bool pop(char* out) {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false; // Empty
        
        std::strcpy(out, data[t]);
        tail.store((t + 1) % CAPACITY, std::memory_order_release);
        return true;
    }
};

class Logger {
    RingBuffer buffer;
    std::thread worker;
    std::atomic<bool> running{true};
    std::ofstream file;
    double sample_rate = 1.0;

    void process() {
        char line[512];
        while (running.load(std::memory_order_relaxed) || buffer.pop(line)) {
            while (buffer.pop(line)) {
                if (file.is_open()) file << line;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

public:
    static Logger& get() { static Logger i; return i; }
    
    Logger() { worker = std::thread(&Logger::process, this); }
    ~Logger() { 
        running.store(false, std::memory_order_release);
        if (worker.joinable()) worker.join();
    }

    void open(const std::string& path) { file.open(path, std::ios::app); }
    void set_sampling(double r) { sample_rate = r; }

    bool should_sample() {
        static thread_local std::mt19937 gen(std::random_device{}());
        static thread_local std::uniform_real_distribution<> dis(0.0, 1.0);
        return dis(gen) <= sample_rate;
    }

    void write(Level lvl, std::string_view msg) {
        if (!ctx.sample && lvl != ERR) return;

        auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        char buf[512];
        const char* tags = ctx.tags_len > 0 ? ctx.tags : "-";
        int len = std::snprintf(buf, sizeof(buf), "%016llx %016llx %016llx %d [%s] %.*s\n",
            (unsigned long long)now, (unsigned long long)ctx.trace_id, 
            (unsigned long long)ctx.span_id, (int)lvl, tags,
            (int)msg.length(), msg.data());

        buffer.push(buf, len);
    }
};

struct Span {
    Context prev;
    std::string name;
    Span(std::string_view n) : name(n) {
        prev = ctx;
        if (ctx.trace_id == 0) {
            ctx.trace_id = gen_id();
            ctx.sample = Logger::get().should_sample();
        }
        ctx.span_id = gen_id();
        Logger::get().write(DBUG, "> " + name);
    }
    ~Span() {
        Logger::get().write(DBUG, "< " + name);
        ctx = prev;
    }
};

inline void add_tag(std::string_view k, std::string_view v) {
    if (ctx.tags_len + k.length() + v.length() + 3 >= sizeof(ctx.tags)) return;
    for (char c : k) ctx.tags[ctx.tags_len++] = (c == ' ' || c == ':') ? '_' : c;
    ctx.tags[ctx.tags_len++] = ':';
    for (char c : v) ctx.tags[ctx.tags_len++] = (c == ' ' || c == ':') ? '_' : c;
    ctx.tags[ctx.tags_len++] = ';';
    ctx.tags[ctx.tags_len] = '\0';
}

#define T_INIT(f) tlog::Logger::get().open(f)
#define T_SAMPLE(r) tlog::Logger::get().set_sampling(r)
#define T_INFO(m) tlog::Logger::get().write(tlog::INFO, m)
#define T_WARN(m) tlog::Logger::get().write(tlog::WARN, m)
#define T_ERR(m)  tlog::Logger::get().write(tlog::ERR, m)
#define T_TAG(k,v) tlog::add_tag(k, v)
#define T_SCOPE(n) tlog::Span _s_##__LINE__(n)

}
