# hyperliquid matching engine

High-performance order matching engine written in c++20. processes real hyperliquid market data through a lock-free, low-latency matching engine with sub-microsecond performance.

## requirements

- **c++ compiler**: clang 14+ or gcc 11+ (c++20 support required)
- **cmake**: 3.20+
- **node.js**: 18+ (for frontend/bridge)

## quick start

```bash
# build the c++ engine
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j4

# run tests
ctest

# run benchmark
./benchmark_engine
```

## live visualization (with engine integration)

the frontend pipes real hyperliquid trade data through your c++ matching engine:

```bash
# install frontend dependencies (first time only)
cd frontend
npm install

# terminal 1: start the bridge server (connects hyperliquid → engine → frontend)
npm run bridge

# terminal 2: start the frontend
npm run dev
# open http://localhost:5173
```

**what this shows:**
- real hyperliquid trades flowing through YOUR matching engine
- per-order latency metrics from YOUR engine (avg, min, max)
- throughput counter showing orders/sec your engine processes
- side-by-side comparison: market data vs engine performance

## architecture

```
hyperliquid websocket → node.js bridge → c++ engine (stdin/stdout) → frontend
                              ↓
                   converts trades to orders
                              ↓
                   pipes to your engine subprocess
                              ↓
                   engine matches, outputs metrics
                              ↓
                   frontend displays YOUR engine's performance
```

## tools

| command | description |
|---------|-------------|
| `./benchmark_engine` | throughput benchmark (~4.8M msgs/sec) |
| `./engine_bridge` | json bridge for frontend integration |
| `./cli_viz` | terminal visualization (simulation) |
| `./cli_live` | terminal with real hyperliquid data |
| `./demo` | simple order book demo |

## performance

benchmark results:
- throughput: ~4.8M msgs/sec
- latency: ~200ns average per order

## license

mit
