package tlog

import (
	"context"
	"fmt"
	"math/rand"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

type Level uint8

const (
	INFO Level = 0
	WARN Level = 1
	ERR  Level = 2
	DBUG Level = 3
)

type ctxKey struct{}

type Context struct {
	TraceID uint64
	SpanID  uint64
	Tags    []string
	Sample  bool
}

var defaultContext = Context{Sample: true}

func GenID() uint64 {
	return rand.Uint64()
}

type Logger struct {
	file       *os.File
	buffer     chan string
	sampleRate float64
	running    atomic.Bool
	wg         sync.WaitGroup
	mu         sync.Mutex
}

var logger *Logger
var once sync.Once

func Get() *Logger {
	once.Do(func() {
		logger = &Logger{
			buffer:     make(chan string, 8192),
			sampleRate: 1.0,
		}
		logger.running.Store(true)
		logger.wg.Add(1)
		go logger.process()
	})
	return logger
}

func (l *Logger) process() {
	defer l.wg.Done()
	for line := range l.buffer {
		l.mu.Lock()
		if l.file != nil {
			l.file.WriteString(line)
		}
		l.mu.Unlock()
	}
}

func (l *Logger) Open(path string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	f, err := os.OpenFile(path, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err == nil {
		l.file = f
	}
}

func (l *Logger) Write(ctx context.Context, lvl Level, msg string) {
	tCtx := FromContext(ctx)
	if !tCtx.Sample && lvl != ERR {
		return
	}

	now := time.Now().UnixNano()
	tagsStr := "-"
	if len(tCtx.Tags) > 0 {
		tagsStr = strings.Join(tCtx.Tags, "")
	}

	line := fmt.Sprintf("%016x %016x %016x %d [%s] %s\n",
		now, tCtx.TraceID, tCtx.SpanID, lvl, tagsStr, msg)

	select {
	case l.buffer <- line:
	default:
		// Ring buffer full behavior: drop
	}
}

func FromContext(ctx context.Context) Context {
	if ctx == nil {
		return defaultContext
	}
	if v, ok := ctx.Value(ctxKey{}).(Context); ok {
		return v
	}
	return defaultContext
}

func WithContext(ctx context.Context, tCtx Context) context.Context {
	return context.WithValue(ctx, ctxKey{}, tCtx)
}

// Helper API
func Init(path string) { Get().Open(path) }
func Sample(rate float64) { Get().sampleRate = rate }

func Info(ctx context.Context, msg string) { Get().Write(ctx, INFO, msg) }
func Warn(ctx context.Context, msg string) { Get().Write(ctx, WARN, msg) }
func Err(ctx context.Context, msg string) { Get().Write(ctx, ERR, msg) }

func AddTag(ctx context.Context, k, v string) context.Context {
	tCtx := FromContext(ctx)
	kSafe := strings.ReplaceAll(strings.ReplaceAll(k, " ", "_"), ":", "_")
	vSafe := strings.ReplaceAll(strings.ReplaceAll(v, " ", "_"), ":", "_")
	
	newTags := make([]string, len(tCtx.Tags), len(tCtx.Tags)+1)
	copy(newTags, tCtx.Tags)
	newTags = append(newTags, fmt.Sprintf("%s:%s;", kSafe, vSafe))
	
tCtx.Tags = newTags
	return WithContext(ctx, tCtx)
}

// Span starts a new span scope. Returns the new context and a finish func.
// Usage:
//   ctx, finish := tlog.Span(ctx, "myOp")
//   defer finish()
func Span(ctx context.Context, name string) (context.Context, func()) {
	tCtx := FromContext(ctx)
	
	if tCtx.TraceID == 0 {
		tCtx.TraceID = GenID()
		tCtx.Sample = rand.Float64() <= Get().sampleRate
	}
	tCtx.SpanID = GenID()
	
	newCtx := WithContext(ctx, tCtx)
	Get().Write(newCtx, DBUG, "> "+name)
	
	return newCtx, func() {
		Get().Write(newCtx, DBUG, "< "+name)
	}
}
