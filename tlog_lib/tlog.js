const fs = require('fs');
const { AsyncLocalStorage } = require('async_hooks');
const crypto = require('crypto');

const Level = { INFO: 0, WARN: 1, ERR: 2, DBUG: 3 };

const asyncLocalStorage = new AsyncLocalStorage();

function getCtx() {
    const store = asyncLocalStorage.getStore();
    if (store) return store;
    // Default context for top-level code (not strictly thread-local but process-global fallback)
    if (!global.tlogDefaultCtx) {
        global.tlogDefaultCtx = { trace_id: 0n, span_id: 0n, tags: [], sample: true };
    }
    return global.tlogDefaultCtx;
}

function gen_id() {
    return  crypto.randomBytes(8).readBigUInt64LE(0);
}

class Logger {
    constructor() {
        this.buffer = [];
        this.capacity = 8192;
        this.running = true;
        this.fileStream = null;
        this.sample_rate = 1.0;
        
        // Background flusher
        this.flushInterval = setInterval(() => this.process(), 5);
        this.flushInterval.unref();
        
        // Ensure flush on exit
        process.on('exit', () => this.process());
    }

    static get() {
        if (!Logger.instance) Logger.instance = new Logger();
        return Logger.instance;
    }

    open(path) {
        this.fileStream = fs.createWriteStream(path, { flags: 'a' });
    }

    set_sampling(r) {
        this.sample_rate = r;
    }

    should_sample() {
        return Math.random() <= this.sample_rate;
    }

    process() {
        if (this.buffer.length === 0) return;
        const toWrite = this.buffer;
        this.buffer = [];
        
        if (this.fileStream) {
            toWrite.forEach(line => this.fileStream.write(line));
        }
    }

    write(lvl, msg) {
        const ctx = getCtx();
        if (!ctx.sample && lvl !== Level.ERR) return;

        // Nanoseconds since epoch (approximate)
        const now = BigInt(Date.now()) * 1000000n; 

        const tags = ctx.tags.length > 0 ? ctx.tags.join('') : '-';
        
        const line = `${now.toString(16).padStart(16, '0')} ${ctx.trace_id.toString(16).padStart(16, '0')} ${ctx.span_id.toString(16).padStart(16, '0')} ${lvl} [${tags}] ${msg}\n`;

        if (this.buffer.length < this.capacity) {
            this.buffer.push(line);
        }
    }
}

function scope(name, fn) {
    const parentCtx = getCtx();
    const newCtx = {
        trace_id: parentCtx.trace_id,
        span_id: parentCtx.span_id,
        tags: [...parentCtx.tags],
        sample: parentCtx.sample
    };

    if (newCtx.trace_id === 0n) {
        newCtx.trace_id = gen_id();
        newCtx.sample = Logger.get().should_sample();
    }
    newCtx.span_id = gen_id();

    return asyncLocalStorage.run(newCtx, () => {
        Logger.get().write(Level.DBUG, `> ${name}`);
        try {
            const res = fn();
            if (res instanceof Promise) {
                return res.finally(() => Logger.get().write(Level.DBUG, `< ${name}`));
            }
            Logger.get().write(Level.DBUG, `< ${name}`);
            return res;
        } catch (e) {
            Logger.get().write(Level.DBUG, `< ${name}`);
            throw e;
        }
    });
}

function add_tag(k, v) {
    const ctx = getCtx();
    const safeK = String(k).replace(/[: ]/g, '_');
    const safeV = String(v).replace(/[: ]/g, '_');
    ctx.tags.push(`${safeK}:${safeV};`);
}

module.exports = {
    Level,
    init: (f) => Logger.get().open(f),
    sample: (r) => Logger.get().set_sampling(r),
    info: (m) => Logger.get().write(Level.INFO, m),
    warn: (m) => Logger.get().write(Level.WARN, m),
    err: (m) => Logger.get().write(Level.ERR, m),
    tag: add_tag,
    scope
};
