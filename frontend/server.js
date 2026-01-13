// Node.js bridge server
// Connects to Hyperliquid, pipes data to C++ engine subprocess, broadcasts to frontend

import WebSocket, { WebSocketServer } from 'ws';
import { spawn } from 'child_process';
import { createInterface } from 'readline';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ENGINE_PATH = join(__dirname, '../build/engine_bridge');

const HYPERLIQUID_WS = 'wss://api.hyperliquid.xyz/ws';
const SERVER_PORT = 3001;
const DEFAULT_COIN = 'BTC';

// state
let engineProcess = null;
let hyperliquidWs = null;
let clients = new Set();
let currentCoin = DEFAULT_COIN;
let isEngineReady = false;

// engine metrics (aggregated)
let engineMetrics = {
    orders_processed: 0,
    trades_executed: 0,
    resting_orders: 0,
    avg_latency_ns: 0,
    min_latency_ns: 0,
    max_latency_ns: 0,
    best_bid: 0,
    best_ask: 0,
    throughput: 0,
    last_update: Date.now()
};

// throughput tracking
let orderCountWindow = [];
const THROUGHPUT_WINDOW_MS = 1000;

function calculateThroughput() {
    const now = Date.now();
    orderCountWindow = orderCountWindow.filter(t => now - t < THROUGHPUT_WINDOW_MS);
    return orderCountWindow.length;
}

// broadcast to all connected clients
function broadcast(message) {
    const data = JSON.stringify(message);
    for (const client of clients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(data);
        }
    }
}

// spawn C++ engine process
function startEngine() {
    console.log('[Bridge] Starting C++ engine...');

    engineProcess = spawn(ENGINE_PATH, [], {
        stdio: ['pipe', 'pipe', 'pipe']
    });

    const rl = createInterface({ input: engineProcess.stdout });

    rl.on('line', (line) => {
        try {
            const event = JSON.parse(line);

            if (event.type === 'ready') {
                isEngineReady = true;
                console.log('[Bridge] C++ engine ready');
                broadcast({ type: 'engine_status', status: 'ready' });
            } else if (event.type === 'stats') {
                // update aggregated metrics
                Object.assign(engineMetrics, event.data);
                engineMetrics.throughput = calculateThroughput();
                engineMetrics.last_update = Date.now();
                broadcast({ type: 'engine_metrics', data: engineMetrics });
            } else if (event.type === 'trade') {
                broadcast({ type: 'engine_trade', data: event.data });
            } else if (event.type === 'book') {
                broadcast({ type: 'engine_book', data: event.data });
            }
        } catch (e) {
            // ignore parse errors
        }
    });

    engineProcess.stderr.on('data', (data) => {
        console.error('[Engine Error]', data.toString());
    });

    engineProcess.on('close', (code) => {
        console.log('[Bridge] Engine process exited with code', code);
        isEngineReady = false;
        broadcast({ type: 'engine_status', status: 'stopped' });
        // restart after delay
        setTimeout(startEngine, 2000);
    });

    engineProcess.on('error', (err) => {
        console.error('[Bridge] Failed to start engine:', err.message);
        broadcast({ type: 'engine_status', status: 'error', message: err.message });
    });
}

// send command to engine
function sendToEngine(cmd) {
    if (engineProcess && isEngineReady) {
        engineProcess.stdin.write(JSON.stringify(cmd) + '\n');
    }
}

// connect to Hyperliquid WebSocket
function connectHyperliquid() {
    console.log('[Bridge] Connecting to Hyperliquid...');

    hyperliquidWs = new WebSocket(HYPERLIQUID_WS);

    hyperliquidWs.on('open', () => {
        console.log('[Bridge] Connected to Hyperliquid');
        // subscribe to trades and order book
        hyperliquidWs.send(JSON.stringify({
            method: 'subscribe',
            subscription: { type: 'trades', coin: currentCoin }
        }));
        hyperliquidWs.send(JSON.stringify({
            method: 'subscribe',
            subscription: { type: 'l2Book', coin: currentCoin }
        }));
        broadcast({ type: 'hyperliquid_status', status: 'connected', coin: currentCoin });
    });

    hyperliquidWs.on('message', (data) => {
        try {
            const msg = JSON.parse(data.toString());

            // forward to frontend
            if (msg.channel === 'trades') {
                broadcast({ type: 'hyperliquid_trades', data: msg.data });

                // convert each trade to an order and send to engine
                for (const trade of msg.data || []) {
                    const orderCmd = {
                        cmd: 'order',
                        price: parseFloat(trade.px),
                        size: parseFloat(trade.sz),
                        side: trade.side
                    };
                    sendToEngine(orderCmd);
                    orderCountWindow.push(Date.now());
                }
            } else if (msg.channel === 'l2Book') {
                broadcast({ type: 'hyperliquid_book', data: msg.data });
            }
        } catch (e) {
            // ignore
        }
    });

    hyperliquidWs.on('close', () => {
        console.log('[Bridge] Hyperliquid connection closed, reconnecting...');
        broadcast({ type: 'hyperliquid_status', status: 'disconnected' });
        setTimeout(connectHyperliquid, 3000);
    });

    hyperliquidWs.on('error', (err) => {
        console.error('[Bridge] Hyperliquid error:', err.message);
    });
}

// change coin
function changeCoin(newCoin) {
    if (hyperliquidWs && hyperliquidWs.readyState === WebSocket.OPEN) {
        // unsubscribe from old
        hyperliquidWs.send(JSON.stringify({
            method: 'unsubscribe',
            subscription: { type: 'trades', coin: currentCoin }
        }));
        hyperliquidWs.send(JSON.stringify({
            method: 'unsubscribe',
            subscription: { type: 'l2Book', coin: currentCoin }
        }));
        // subscribe to new
        hyperliquidWs.send(JSON.stringify({
            method: 'subscribe',
            subscription: { type: 'trades', coin: newCoin }
        }));
        hyperliquidWs.send(JSON.stringify({
            method: 'subscribe',
            subscription: { type: 'l2Book', coin: newCoin }
        }));
    }
    currentCoin = newCoin;

    // reset engine
    sendToEngine({ cmd: 'reset' });
    orderCountWindow = [];
    engineMetrics = {
        orders_processed: 0,
        trades_executed: 0,
        resting_orders: 0,
        avg_latency_ns: 0,
        min_latency_ns: 0,
        max_latency_ns: 0,
        best_bid: 0,
        best_ask: 0,
        throughput: 0,
        last_update: Date.now()
    };

    broadcast({ type: 'coin_changed', coin: newCoin });
}

// start WebSocket server for frontend clients
function startServer() {
    const wss = new WebSocketServer({ port: SERVER_PORT });

    wss.on('connection', (ws) => {
        console.log('[Bridge] Frontend client connected');
        clients.add(ws);

        // send current state
        ws.send(JSON.stringify({
            type: 'init',
            coin: currentCoin,
            engine_ready: isEngineReady,
            metrics: engineMetrics
        }));

        ws.on('message', (data) => {
            try {
                const msg = JSON.parse(data.toString());
                if (msg.type === 'change_coin') {
                    changeCoin(msg.coin);
                } else if (msg.type === 'request_stats') {
                    sendToEngine({ cmd: 'stats' });
                }
            } catch (e) {
                // ignore
            }
        });

        ws.on('close', () => {
            clients.delete(ws);
            console.log('[Bridge] Frontend client disconnected');
        });
    });

    console.log(`[Bridge] WebSocket server listening on port ${SERVER_PORT}`);
}

// periodic stats request
setInterval(() => {
    sendToEngine({ cmd: 'stats' });
}, 500);

// main
console.log('[Bridge] Hyperliquid ↔ C++ Engine Bridge');
console.log('[Bridge] Engine path:', ENGINE_PATH);

startEngine();
setTimeout(connectHyperliquid, 1000);
startServer();
