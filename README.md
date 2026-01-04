# tlog

Minimal, single-header tracing logger for C++ with browser-based analysis.

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

### CLI
```bash
tlog tail app.log                     # Live stream
tlog json app.log > out.json          # Export
tlog diff app.log <id1> <id2>         # Compare traces
```

### Browser Viewer
Open `viewer.html`, drag in your `.log` file.

**Views:**
- **Simple** – Indented tree with timing
- **Gantt** – Horizontal bars showing scope durations
- **Heatmap** – Scatter plot of all traces (X=time, Y=latency, red=error)


## Log Format
Fixed-width columnar:

TIMESTAMP        TRACE_ID         SPAN_ID          LVL [TAGS] MSG

17a2b9c0e1200000 8f1e2a3b4c5d6e7f 1111111111111111 2 [user:alice;] Card declined
