import React, { useState, useEffect, useMemo, useRef, useCallback } from 'react';
import { createChart, ColorType, CrosshairMode, CandlestickSeries, HistogramSeries } from 'lightweight-charts';
import { Activity, TrendingUp, Zap, Clock, ChevronUp, ChevronDown, BarChart3, Wifi, WifiOff, Cpu, Timer, Database, GitBranch } from 'lucide-react';

// --- Config ---
const BRIDGE_WS = 'ws://localhost:3001';
const API_URL = 'https://api.hyperliquid.xyz/info';
const DEFAULT_COIN = 'BTC';
const DEFAULT_INTERVAL = '1m';

const INTERVALS = [
  { label: '1m', value: '1m' },
  { label: '5m', value: '5m' },
  { label: '15m', value: '15m' },
  { label: '1h', value: '1h' },
  { label: '4h', value: '4h' },
  { label: '1d', value: '1d' },
];

// --- Utils ---
function cn(...classes) {
  return classes.filter(Boolean).join(' ');
}

function formatNumber(num) {
  if (num >= 1000000) return (num / 1000000).toFixed(2) + 'M';
  if (num >= 1000) return (num / 1000).toFixed(1) + 'k';
  return typeof num === 'number' ? num.toFixed(2) : '0';
}

function formatPrice(price) {
  return parseFloat(price).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
}

function formatLatency(ns) {
  if (ns >= 1000000) return (ns / 1000000).toFixed(2) + 'ms';
  if (ns >= 1000) return (ns / 1000).toFixed(1) + 'µs';
  return ns + 'ns';
}

// Format crypto size with more precision
function formatSize(num) {
  if (num >= 1000) return num.toFixed(2);
  if (num >= 100) return num.toFixed(3);
  if (num >= 10) return num.toFixed(4);
  if (num >= 1) return num.toFixed(5);
  return num.toFixed(6);
}

// Fetch historical candles
async function fetchCandles(coin, interval, count = 300) {
  const endTime = Date.now();
  const intervalMs = {
    '1m': 60 * 1000, '5m': 5 * 60 * 1000, '15m': 15 * 60 * 1000,
    '1h': 60 * 60 * 1000, '4h': 4 * 60 * 60 * 1000, '1d': 24 * 60 * 60 * 1000,
  };
  const startTime = endTime - (intervalMs[interval] || 60000) * count;

  try {
    const response = await fetch(API_URL, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ type: 'candleSnapshot', req: { coin, interval, startTime, endTime } })
    });
    const data = await response.json();
    return data.map(c => ({
      time: Math.floor(c.t / 1000),
      open: parseFloat(c.o),
      high: parseFloat(c.h),
      low: parseFloat(c.l),
      close: parseFloat(c.c),
      volume: parseFloat(c.v),
    }));
  } catch (err) {
    console.error('Failed to fetch candles:', err);
    return [];
  }
}

// --- TradingView Chart Component ---
const TradingViewChart = ({ coin, interval, trades }) => {
  const chartContainerRef = useRef(null);
  const chartRef = useRef(null);
  const candleSeriesRef = useRef(null);
  const volumeSeriesRef = useRef(null);
  const [isReady, setIsReady] = useState(false);
  const [isLoading, setIsLoading] = useState(true);

  useEffect(() => {
    if (!chartContainerRef.current) return;
    const timer = setTimeout(() => {
      try {
        const container = chartContainerRef.current;
        if (!container || container.clientWidth === 0) return;

        const chart = createChart(container, {
          width: container.clientWidth,
          height: container.clientHeight,
          layout: { background: { type: ColorType.Solid, color: 'transparent' }, textColor: '#666' },
          grid: { vertLines: { color: 'rgba(255, 255, 255, 0.03)' }, horzLines: { color: 'rgba(255, 255, 255, 0.03)' } },
          crosshair: { mode: CrosshairMode.Normal },
          rightPriceScale: { borderColor: 'rgba(255, 255, 255, 0.1)', scaleMargins: { top: 0.1, bottom: 0.2 } },
          timeScale: { borderColor: 'rgba(255, 255, 255, 0.1)', timeVisible: true, secondsVisible: interval === '1m' },
        });

        const candleSeries = chart.addSeries(CandlestickSeries, {
          upColor: '#10b981', downColor: '#ef4444',
          borderUpColor: '#10b981', borderDownColor: '#ef4444',
          wickUpColor: '#10b981', wickDownColor: '#ef4444',
        });

        const volumeSeries = chart.addSeries(HistogramSeries, {
          color: '#26a69a', priceFormat: { type: 'volume' }, priceScaleId: '',
        });
        volumeSeries.priceScale().applyOptions({ scaleMargins: { top: 0.85, bottom: 0 } });

        chartRef.current = chart;
        candleSeriesRef.current = candleSeries;
        volumeSeriesRef.current = volumeSeries;
        setIsReady(true);

        const resizeObserver = new ResizeObserver(entries => {
          if (entries[0] && chartRef.current) {
            const { width, height } = entries[0].contentRect;
            if (width > 0 && height > 0) chartRef.current.applyOptions({ width, height });
          }
        });
        resizeObserver.observe(container);

        return () => {
          resizeObserver.disconnect();
          if (chartRef.current) { chartRef.current.remove(); chartRef.current = null; }
        };
      } catch (err) { console.error('Chart init error:', err); }
    }, 100);

    return () => clearTimeout(timer);
  }, []);

  useEffect(() => {
    if (!isReady || !candleSeriesRef.current) return;
    setIsLoading(true);
    fetchCandles(coin, interval).then(candles => {
      if (candles.length > 0 && candleSeriesRef.current) {
        candleSeriesRef.current.setData(candles);
        volumeSeriesRef.current.setData(
          candles.map(c => ({ time: c.time, value: c.volume, color: c.close >= c.open ? 'rgba(16, 185, 129, 0.5)' : 'rgba(239, 68, 68, 0.5)' }))
        );
        chartRef.current?.timeScale().fitContent();
      }
      setIsLoading(false);
    });
  }, [coin, interval, isReady]);

  return (
    <div ref={chartContainerRef} className="w-full h-full relative" style={{ minHeight: '200px' }}>
      {isLoading && (
        <div className="absolute inset-0 flex items-center justify-center bg-black/50 z-10">
          <div className="text-neutral-400 text-sm animate-pulse">Loading {interval} candles...</div>
        </div>
      )}
    </div>
  );
};

// --- Components ---
const GlassCard = ({ children, className, glow, accent }) => (
  <div className={cn(
    "relative rounded-xl border bg-gradient-to-br from-white/5 to-transparent backdrop-blur-sm",
    accent === 'green' ? "border-emerald-500/30" : accent === 'blue' ? "border-blue-500/30" : "border-white/10",
    glow && "shadow-lg shadow-white/5",
    className
  )}>
    <div className="absolute inset-0 rounded-xl bg-gradient-to-br from-white/5 via-transparent to-transparent pointer-events-none" />
    <div className="relative z-10 h-full">{children}</div>
  </div>
);

const StatCard = ({ label, value, subvalue, icon: Icon, trend, pulse, accent }) => (
  <GlassCard className="p-3" glow accent={accent}>
    <div className="flex items-start justify-between">
      <div className="flex-1">
        <p className="text-[10px] font-medium uppercase tracking-wider text-neutral-500 mb-0.5">{label}</p>
        <p className={cn("text-lg font-bold tracking-tight text-white", pulse && "animate-pulse")}>{value}</p>
        {subvalue && <p className="text-[10px] text-neutral-500">{subvalue}</p>}
        {trend !== undefined && trend !== 0 && (
          <div className={cn("flex items-center gap-1 mt-0.5 text-[10px] font-medium", trend > 0 ? "text-emerald-400" : "text-rose-400")}>
            {trend > 0 ? <ChevronUp className="w-3 h-3" /> : <ChevronDown className="w-3 h-3" />}
            {Math.abs(trend).toFixed(2)}%
          </div>
        )}
      </div>
      <div className={cn("p-1.5 rounded-lg", accent === 'green' ? "bg-emerald-500/10" : accent === 'blue' ? "bg-blue-500/10" : "bg-white/5")}>
        <Icon className={cn("w-3 h-3", accent === 'green' ? "text-emerald-400" : accent === 'blue' ? "text-blue-400" : "text-neutral-400")} />
      </div>
    </div>
  </GlassCard>
);

const OrderBookLevel = ({ price, size, side, maxSize, isTop }) => {
  const pct = Math.min((size / maxSize) * 100, 100);
  const isBid = side === 'bid';

  return (
    <div className={cn("relative flex items-center justify-between px-2 py-0.5 font-mono text-[10px]", isTop && isBid && "bg-emerald-500/10", isTop && !isBid && "bg-rose-500/10")}>
      <div className={cn("absolute top-0 h-full opacity-20", isBid ? "right-0 bg-gradient-to-l from-emerald-500" : "left-0 bg-gradient-to-r from-rose-500")} style={{ width: `${pct}%` }} />
      <span className="relative z-10 text-neutral-400">{formatNumber(size)}</span>
      <span className={cn("relative z-10 font-medium", isBid ? "text-emerald-400" : "text-rose-400")}>{formatPrice(price)}</span>
    </div>
  );
};

const TradeRow = ({ trade, isNew, index }) => {
  const [animate, setAnimate] = useState(isNew);

  useEffect(() => {
    if (isNew) {
      setAnimate(true);
      const timer = setTimeout(() => setAnimate(false), 600);
      return () => clearTimeout(timer);
    }
  }, [isNew]);

  return (
    <div className={cn(
      "grid grid-cols-3 gap-1 px-2 py-0.5 text-[10px] font-mono border-b border-white/5 transition-all",
      animate && "bg-gradient-to-r from-white/10 via-white/5 to-transparent animate-[slideIn_0.3s_ease-out]",
      index === 0 && "shadow-[0_0_10px_rgba(255,255,255,0.1)]"
    )}>
      <span className={cn("font-medium", trade.side === 'B' ? "text-emerald-400" : "text-rose-400")}>{formatPrice(trade.px)}</span>
      <span className="text-neutral-400 text-right">{formatSize(parseFloat(trade.sz))}</span>
      <span className="text-neutral-600 text-right">{new Date(trade.time).toLocaleTimeString()}</span>
    </div>
  );
};

export default function App() {
  const [bridgeConnected, setBridgeConnected] = useState(false);
  const [engineReady, setEngineReady] = useState(false);
  const [hyperliquidConnected, setHyperliquidConnected] = useState(false);
  const [coin, setCoin] = useState(DEFAULT_COIN);
  const [interval, setIntervalState] = useState(DEFAULT_INTERVAL);
  const [orderBook, setOrderBook] = useState({ bids: [], asks: [] });
  const [trades, setTrades] = useState([]);
  const [stats, setStats] = useState({ lastPrice: 0, priceChange: 0, tradeCount: 0, volume: 0 });

  // Engine metrics
  const [engineMetrics, setEngineMetrics] = useState({
    orders_processed: 0,
    trades_executed: 0,
    avg_latency_ns: 0,
    min_latency_ns: 0,
    max_latency_ns: 0,
    throughput: 0,
    resting_orders: 0,
  });

  const wsRef = useRef(null);
  const lastPriceRef = useRef(0);
  const tradeCounterRef = useRef(0);

  // Connect to bridge server
  useEffect(() => {
    const connect = () => {
      const ws = new WebSocket(BRIDGE_WS);
      wsRef.current = ws;

      ws.onopen = () => {
        setBridgeConnected(true);
        console.log('Connected to bridge server');
      };

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);

          switch (msg.type) {
            case 'init':
              setEngineReady(msg.engine_ready);
              if (msg.metrics) setEngineMetrics(msg.metrics);
              break;
            case 'engine_status':
              setEngineReady(msg.status === 'ready');
              break;
            case 'engine_metrics':
              setEngineMetrics(msg.data);
              break;
            case 'hyperliquid_status':
              setHyperliquidConnected(msg.status === 'connected');
              break;
            case 'hyperliquid_book':
              if (msg.data && msg.data.levels) {
                setOrderBook({ bids: msg.data.levels[0] || [], asks: msg.data.levels[1] || [] });
              }
              break;
            case 'hyperliquid_trades':
              if (msg.data && msg.data.length > 0) {
                // Generate unique IDs using counter to avoid React key collisions
                const incomingTrades = msg.data.map(t => {
                  tradeCounterRef.current += 1;
                  return { ...t, _uid: tradeCounterRef.current };
                });
                setTrades(prev => [...incomingTrades, ...prev].slice(0, 100));
                const latest = msg.data[0];
                const price = parseFloat(latest.px);
                const size = parseFloat(latest.sz);
                setStats(prev => {
                  const change = lastPriceRef.current ? ((price - lastPriceRef.current) / lastPriceRef.current) * 100 : 0;
                  lastPriceRef.current = price;
                  return { lastPrice: price, priceChange: change, tradeCount: prev.tradeCount + msg.data.length, volume: prev.volume + price * size };
                });
              }
              break;
            case 'coin_changed':
              setTrades([]);
              setStats({ lastPrice: 0, priceChange: 0, tradeCount: 0, volume: 0 });
              lastPriceRef.current = 0;
              break;
          }
        } catch (e) { }
      };

      ws.onclose = () => {
        setBridgeConnected(false);
        setEngineReady(false);
        setHyperliquidConnected(false);
        setTimeout(connect, 3000);
      };

      ws.onerror = () => ws.close();
    };

    connect();
    return () => { if (wsRef.current) wsRef.current.close(); };
  }, []);

  const changeCoin = (newCoin) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify({ type: 'change_coin', coin: newCoin }));
    }
    setCoin(newCoin);
  };

  const maxBidSize = useMemo(() => Math.max(...orderBook.bids.map(b => parseFloat(b.sz) || 0), 1), [orderBook.bids]);
  const maxAskSize = useMemo(() => Math.max(...orderBook.asks.map(a => parseFloat(a.sz) || 0), 1), [orderBook.asks]);
  const maxSize = Math.max(maxBidSize, maxAskSize);
  const bestBid = orderBook.bids[0];
  const bestAsk = orderBook.asks[0];
  const spread = bestBid && bestAsk ? (parseFloat(bestAsk.px) - parseFloat(bestBid.px)).toFixed(2) : '—';
  const midPrice = bestBid && bestAsk ? (parseFloat(bestAsk.px) + parseFloat(bestBid.px)) / 2 : 0;
  const coins = ['BTC', 'ETH', 'SOL', 'ARB', 'DOGE'];

  return (
    <div className="min-h-screen bg-black text-white selection:bg-white/20">
      <div className="fixed inset-0 bg-gradient-to-br from-neutral-950 via-black to-neutral-950 pointer-events-none" />
      <div className="fixed inset-0 bg-[radial-gradient(ellipse_at_top,_var(--tw-gradient-stops))] from-blue-500/5 via-transparent to-transparent pointer-events-none" />

      <div className="relative z-10 p-3 max-w-[1920px] mx-auto h-screen flex flex-col">
        {/* Header */}
        <header className="flex items-center justify-between mb-3 flex-shrink-0">
          <div className="flex items-center gap-4">
            <div className="relative">
              <div className="w-9 h-9 rounded-xl bg-gradient-to-br from-blue-500 to-blue-600 flex items-center justify-center">
                <Cpu className="w-4 h-4 text-white" />
              </div>
              <div className={cn("absolute -bottom-0.5 -right-0.5 w-2.5 h-2.5 rounded-full border-2 border-black", engineReady ? "bg-emerald-500" : "bg-rose-500")} />
            </div>
            <div>
              <h1 className="text-lg font-bold tracking-tight">MATCHING ENGINE</h1>
              <div className="flex items-center gap-2 text-[10px] font-mono tracking-wider">
                <span className={cn("flex items-center gap-1", bridgeConnected ? "text-emerald-500" : "text-rose-500")}>
                  {bridgeConnected ? <Wifi className="w-2.5 h-2.5" /> : <WifiOff className="w-2.5 h-2.5" />}
                  BRIDGE
                </span>
                <span className="text-neutral-600">|</span>
                <span className={cn("flex items-center gap-1", engineReady ? "text-blue-500" : "text-rose-500")}>
                  <Cpu className="w-2.5 h-2.5" />
                  ENGINE
                </span>
                <span className="text-neutral-600">|</span>
                <span className={cn("flex items-center gap-1", hyperliquidConnected ? "text-emerald-500" : "text-rose-500")}>
                  <GitBranch className="w-2.5 h-2.5" />
                  HYPERLIQUID
                </span>
              </div>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <div className="flex items-center gap-1 bg-white/5 rounded-lg p-1">
              {coins.map(c => (
                <button key={c} onClick={() => changeCoin(c)} className={cn("px-2 py-1 text-[10px] font-mono rounded-md transition-all", coin === c ? "bg-white text-black" : "text-neutral-400 hover:text-white")}>{c}</button>
              ))}
            </div>
            <div className="flex items-center gap-1 bg-white/5 rounded-lg p-1">
              {INTERVALS.map(i => (
                <button key={i.value} onClick={() => setIntervalState(i.value)} className={cn("px-2 py-1 text-[10px] font-mono rounded-md transition-all", interval === i.value ? "bg-white text-black" : "text-neutral-400 hover:text-white")}>{i.label}</button>
              ))}
            </div>
          </div>
        </header>

        {/* Main Grid */}
        <div className="flex-1 grid grid-cols-12 gap-2 min-h-0 overflow-hidden">

          {/* Left - Engine Metrics */}
          <div className="col-span-2 flex flex-col gap-2 overflow-hidden">
            <div className="text-[10px] font-bold uppercase tracking-wider text-blue-400 px-1">C++ Engine</div>
            <StatCard label="Throughput" value={formatNumber(engineMetrics.throughput) + '/s'} subvalue="orders processed" icon={Zap} accent="blue" pulse={engineReady} />
            <StatCard label="Avg Latency" value={formatLatency(engineMetrics.avg_latency_ns)} subvalue={`min: ${formatLatency(engineMetrics.min_latency_ns)}`} icon={Timer} accent="blue" />
            <StatCard label="Orders" value={formatNumber(engineMetrics.orders_processed)} subvalue={`${engineMetrics.resting_orders} resting`} icon={Database} accent="blue" />
            <StatCard label="Matches" value={formatNumber(engineMetrics.trades_executed)} icon={Activity} accent="blue" />

            <div className="text-[10px] font-bold uppercase tracking-wider text-neutral-400 px-1 mt-2">Market</div>
            <StatCard label="Last Price" value={stats.lastPrice ? formatPrice(stats.lastPrice) : '—'} subvalue={coin + '-USD'} icon={TrendingUp} trend={stats.priceChange} />
            <StatCard label="Spread" value={'$' + spread} icon={Clock} />
          </div>

          {/* Center - Chart */}
          <div className="col-span-7 flex flex-col gap-2 min-h-0 overflow-hidden">
            <GlassCard className="flex-1 p-3 flex flex-col min-h-0" glow accent="blue">
              <div className="flex items-start justify-between mb-2 flex-shrink-0">
                <div>
                  <p className="text-[10px] font-medium uppercase tracking-wider text-neutral-500">{coin}-USD • {interval}</p>
                  <div className="flex items-baseline gap-2 mt-0.5">
                    <span className="text-2xl font-bold font-mono">{midPrice ? formatPrice(midPrice) : '—'}</span>
                    {stats.priceChange !== 0 && (
                      <span className={cn("text-xs font-medium", stats.priceChange > 0 ? "text-emerald-400" : "text-rose-400")}>
                        {stats.priceChange > 0 ? '+' : ''}{stats.priceChange.toFixed(3)}%
                      </span>
                    )}
                  </div>
                </div>
                <div className="text-right">
                  <p className="text-[10px] text-neutral-500">Engine Status</p>
                  <p className={cn("text-xs font-mono font-bold", engineReady ? "text-emerald-400" : "text-rose-400")}>
                    {engineReady ? 'PROCESSING' : 'OFFLINE'}
                  </p>
                </div>
              </div>
              <div className="flex-1 min-h-0">
                <TradingViewChart coin={coin} interval={interval} trades={trades} />
              </div>
            </GlassCard>
          </div>

          {/* Right - Order Book & Trades */}
          <div className="col-span-3 flex flex-col gap-2 min-h-0 overflow-hidden">
            <GlassCard className="flex-1 overflow-hidden flex flex-col" glow>
              <div className="p-2 border-b border-white/5 flex items-center justify-between flex-shrink-0">
                <p className="text-[10px] font-medium uppercase tracking-wider text-neutral-500">Order Book</p>
                <span className="text-[9px] text-neutral-600 font-mono">{coin}/USD</span>
              </div>
              <div className="flex-1 flex flex-col min-h-0 overflow-hidden">
                <div className="flex-1 flex flex-col justify-end overflow-hidden">
                  <div className="px-2 py-0.5 text-[9px] font-mono text-neutral-600 flex justify-between border-b border-white/5">
                    <span>SIZE</span><span>PRICE</span>
                  </div>
                  {orderBook.asks.slice(0, 5).reverse().map((level, i) => (
                    <OrderBookLevel key={i} price={level.px} size={parseFloat(level.sz)} side="ask" maxSize={maxSize} isTop={i === 4} />
                  ))}
                </div>
                <div className="px-2 py-1 bg-white/5 border-y border-white/10 flex items-center justify-between flex-shrink-0">
                  <span className="text-[9px] text-neutral-500 font-mono">SPREAD</span>
                  <span className="text-xs font-bold font-mono">${spread}</span>
                </div>
                <div className="flex-1 overflow-hidden">
                  {orderBook.bids.slice(0, 5).map((level, i) => (
                    <OrderBookLevel key={i} price={level.px} size={parseFloat(level.sz)} side="bid" maxSize={maxSize} isTop={i === 0} />
                  ))}
                </div>
              </div>
            </GlassCard>

            <GlassCard className="flex-1 overflow-hidden flex flex-col" glow>
              <div className="p-2 border-b border-white/5 flex items-center justify-between flex-shrink-0">
                <p className="text-[10px] font-medium uppercase tracking-wider text-neutral-500">Recent Trades</p>
                <div className={cn("w-1.5 h-1.5 rounded-full", hyperliquidConnected ? "bg-emerald-500 animate-pulse" : "bg-rose-500")} />
              </div>
              <div className="px-2 py-0.5 text-[9px] font-mono text-neutral-600 grid grid-cols-3 gap-1 border-b border-white/5 flex-shrink-0">
                <span>PRICE</span><span className="text-right">SIZE</span><span className="text-right">TIME</span>
              </div>
              <div className="flex-1 overflow-y-auto">
                {trades.slice(0, 25).map((t, i) => (
                  <TradeRow key={t._uid} trade={t} isNew={i < 3} index={i} />
                ))}
                {trades.length === 0 && <div className="flex items-center justify-center h-full text-neutral-600 text-[10px]">Waiting for trades...</div>}
              </div>
            </GlassCard>
          </div>
        </div>
      </div>
    </div>
  );
}
