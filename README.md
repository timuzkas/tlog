# tlog

Minimal, single-header tracing logger for C++ with high-performance browser analysis and a feature-rich CLI.

## Quick Start

### Instrument Your Code
```cpp
#include "tlog.hpp"

int main() {
    T_INIT("app.log");
    T_SAMPLE(0.1); // Keep 10% of traces, but 100% of errors

    T_SCOPE("request");
    T_TAG("user", "alice");
    T_INFO("Processing payment");
    T_ERR("Card declined");
}
```

### CLI Analysis
The `tlog` utility provides immediate terminal-based insights:

```bash
# General analysis
tlog stats app.log                     # Latency p95/p99, error rates, event counts
tlog tail app.log                      # Real-time stream of incoming traces
tlog scan app.log "user:alice"         # Filtered event list with timestamps

# Deep dive
tlog trace app.log <id_or_prefix>      # Render beautiful indented tree for one trace
tlog diff app.log <id1> <id2>          # Side-by-side comparison of two trace executions

# Export
tlog json app.log > data.json          # Convert to JSON for custom processing
```

### Browser Viewer
Open `viewer.html` and drag in your `.log` file.
/ or use it [here](https://timuzkas.github.io/tlog/)

**Key Features:**
- **Heatmap Explorer**
- **Dynamic Filtering** – by tag (`user:alice`), duration (`>500`), or level (`err`).
- **Gantt Chart** – Visualizes nested `T_SCOPE` timings.

## Log Format
Fixed-width columnar for fast grep/awk/sed:

`TIMESTAMP(hex) TRACE_ID(hex) SPAN_ID(hex) LVL [TAGS;] MSG`

```text
17a2b9c0e1200000 8f1e2a3b4c5d6e7f 1111111111111111 2 [user:alice;] Card declined
```
