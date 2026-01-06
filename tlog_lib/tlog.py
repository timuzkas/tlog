import threading
import time
import os
import random
import sys
from enum import Enum
from queue import Queue

class Level(Enum):
    INFO = 0
    WARN = 1
    ERR = 2
    DBUG = 3

class Context(threading.local):
    def __init__(self):
        self.trace_id = 0
        self.span_id = 0
        self.tags = []
        self.sample = True

ctx = Context()

def gen_id():
    return random.getrandbits(64)

class Logger:
    _instance = None
    
    @classmethod
    def get(cls):
        if cls._instance is None:
            cls._instance = Logger()
        return cls._instance

    def __init__(self):
        self.buffer = Queue(maxsize=8192)
        self.running = True
        self.file = None
        self.sample_rate = 1.0
        self.worker = threading.Thread(target=self.process, daemon=True)
        self.worker.start()

    def process(self):
        while self.running:
            try:
                # Block for a short time to avoid busy waiting, but check running flag often
                line = self.buffer.get(timeout=0.1) 
                if self.file:
                    self.file.write(line)
                    # Simple flushing strategy: flush if queue is empty or periodically
                    if self.buffer.empty():
                        self.file.flush()
                self.buffer.task_done()
            except:
                pass
        
        # Drain remaining
        while not self.buffer.empty():
            try:
                if self.file: self.file.write(self.buffer.get_nowait())
            except: break

    def open(self, path):
        self.file = open(path, "a")

    def set_sampling(self, rate):
        self.sample_rate = rate

    def should_sample(self):
        return random.random() <= self.sample_rate

    def write(self, level, msg):
        if not getattr(ctx, 'sample', True) and level != Level.ERR:
            return
        
        # Ensure context attributes exist (threading.local init behavior varies)
        if not hasattr(ctx, 'trace_id'): ctx.trace_id = 0
        if not hasattr(ctx, 'span_id'): ctx.span_id = 0
        if not hasattr(ctx, 'tags'): ctx.tags = []

        now = int(time.time() * 1e9)
        tags_str = "".join(ctx.tags) if ctx.tags else "-"
        
        log_line = f"{now:016x} {ctx.trace_id:016x} {ctx.span_id:016x} {level.value} [{tags_str}] {msg}\n"
        
        if not self.buffer.full():
            self.buffer.put(log_line)

    def close(self):
        self.running = False
        self.worker.join()
        if self.file:
            self.file.close()

class Span:
    def __init__(self, name):
        self.name = name
        # Snapshot previous state
        if not hasattr(ctx, 'trace_id'): ctx.trace_id = 0
        if not hasattr(ctx, 'span_id'): ctx.span_id = 0
        if not hasattr(ctx, 'tags'): ctx.tags = []
        if not hasattr(ctx, 'sample'): ctx.sample = True

        self.prev_trace_id = ctx.trace_id
        self.prev_span_id = ctx.span_id
        self.prev_tags = list(ctx.tags)
        self.prev_sample = ctx.sample

    def __enter__(self):
        if ctx.trace_id == 0:
            ctx.trace_id = gen_id()
            ctx.sample = Logger.get().should_sample()
        ctx.span_id = gen_id()
        Logger.get().write(Level.DBUG, f"> {self.name}")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        Logger.get().write(Level.DBUG, f"< {self.name}")
        ctx.trace_id = self.prev_trace_id
        ctx.span_id = self.prev_span_id
        ctx.tags = self.prev_tags
        ctx.sample = self.prev_sample

def add_tag(key, value):
    if not hasattr(ctx, 'tags'): ctx.tags = []
    k = str(key).replace(" ", "_").replace(":", "_")
    v = str(value).replace(" ", "_").replace(":", "_")
    ctx.tags.append(f"{k}:{v};")

# Convenience Functions
def init(path): Logger.get().open(path)
def sample(rate): Logger.get().set_sampling(rate)
def info(msg): Logger.get().write(Level.INFO, msg)
def warn(msg): Logger.get().write(Level.WARN, msg)
def err(msg): Logger.get().write(Level.ERR, msg)
def tag(k, v): add_tag(k, v)
