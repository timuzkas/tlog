local tlog = {}
local unpack = unpack or table.unpack

tlog.Level = { INFO = 0, WARN = 1, ERR = 2, DBUG = 3 }

math.randomseed(math.floor(os.time() + (os.clock() * 1000)))

tlog.ctx = {
    trace_id = nil,
    span_id = nil,
    tags = {},
    sample = true
}

local function gen_id()
    return string.format("%08x%08x", math.random(0, 0x7FFFFFFF), math.random(0, 0x7FFFFFFF))
end

local Logger = {
    file = nil,
    sample_rate = 1.0
}

function Logger:open(path)
    self.file = io.open(path, "a")
end

function Logger:set_sampling(r)
    self.sample_rate = r
end

function Logger:should_sample()
    return math.random() <= self.sample_rate
end

function Logger:write(lvl, msg)
    if not tlog.ctx.sample and lvl ~= tlog.Level.ERR then return end
    
    -- Improvement: Combine os.time (epoch) and os.clock (precision)
    -- This creates a high-resolution hex timestamp the viewer can diff.
    local now_s = os.time()
    local micro = math.floor((os.clock() % 1) * 1000000)
    local timestamp = (now_s * 1000000) + micro

    local tag_str = #tlog.ctx.tags > 0 and table.concat(tlog.ctx.tags, "") or "-"
    local tid = tlog.ctx.trace_id or "0000000000000000"
    local sid = tlog.ctx.span_id or "0000000000000000"
    
    -- Format: timestamp is now %016x (microsecond precision)
    local line = string.format("%016x %s %s %d [%s] %s\n", 
        timestamp, tid, sid, lvl, tag_str, msg)
        
    if self.file then
        self.file:write(line)
        self.file:flush()
    else
        print(line:sub(1, -2))
    end
end

function tlog.init(f) Logger:open(f) end
function tlog.sample(r) Logger:set_sampling(r) end
function tlog.info(m) Logger:write(tlog.Level.INFO, m) end
function tlog.warn(m) Logger:write(tlog.Level.WARN, m) end
function tlog.err(m)  Logger:write(tlog.Level.ERR, m) end

function tlog.tag(k, v)
    local k_s = string.gsub(tostring(k), "[:%s;]", "_")
    local v_s = string.gsub(tostring(v), "[:%s;]", "_")
    table.insert(tlog.ctx.tags, k_s .. ":" .. v_s .. ";")
end

function tlog.scope(name, func, ...)
    local p_tid = tlog.ctx.trace_id
    local p_sid = tlog.ctx.span_id
    local p_smp = tlog.ctx.sample
    local p_tgs = {}
    for i, v in ipairs(tlog.ctx.tags) do p_tgs[i] = v end
    
    if not tlog.ctx.trace_id then
        tlog.ctx.trace_id = gen_id()
        tlog.ctx.sample = Logger:should_sample()
    end
    tlog.ctx.span_id = gen_id()
    
    Logger:write(tlog.Level.DBUG, "> " .. name)
    local res = { pcall(func, ...) }
    Logger:write(tlog.Level.DBUG, "< " .. name)
    
    tlog.ctx.trace_id = p_tid
    tlog.ctx.span_id = p_sid
    tlog.ctx.tags = p_tgs
    tlog.ctx.sample = p_smp
    
    if not res[1] then error(res[2]) end
    return select(2, unpack(res))
end

return tlog
