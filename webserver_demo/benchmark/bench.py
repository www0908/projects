#!/usr/bin/env python3
"""Multi-concurrency HTTP benchmark suite using asyncio + aiohttp."""

import argparse
import asyncio
import json
import math
import time
from pathlib import Path
from typing import Dict, List

import aiohttp

DEFAULT_CONCURRENCIES = [100, 500, 1000, 5000, 10000]


def percentile(sorted_values: List[float], p: float) -> float:
    """Compute linear-interpolated percentile from a sorted list."""
    if not sorted_values:
        return 0.0
    k = (len(sorted_values) - 1) * (p / 100.0)
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return sorted_values[int(k)]
    return sorted_values[f] * (c - k) + sorted_values[c] * (k - f)


async def run_level(url: str, concurrency: int, duration: float, timeout_sec: float) -> Dict:
    """Run a single concurrency gradient for `duration` seconds."""
    latencies_ms: List[float] = []
    errors = 0
    stop_event = asyncio.Event()

    connector = aiohttp.TCPConnector(limit=0, ttl_dns_cache=300)
    timeout = aiohttp.ClientTimeout(total=timeout_sec)

    async def worker(session: aiohttp.ClientSession, start_delay: float):
        nonlocal errors
        if start_delay > 0:
            await asyncio.sleep(start_delay)
        while not stop_event.is_set():
            t0 = time.perf_counter()
            try:
                async with session.get(url) as resp:
                    await resp.read()
                    if resp.status < 400:
                        latencies_ms.append((time.perf_counter() - t0) * 1000.0)
                    else:
                        errors += 1
            except Exception:
                errors += 1

    async with aiohttp.ClientSession(connector=connector, timeout=timeout) as session:
        t_start = time.perf_counter()
        # Stagger initial connection burst slightly for high concurrency to avoid local SYN queue drops
        stagger_window = min(0.3, concurrency / 50000.0)
        tasks = [
            asyncio.create_task(worker(session, (i / max(1, concurrency)) * stagger_window))
            for i in range(concurrency)
        ]
        await asyncio.sleep(duration)
        stop_event.set()
        await asyncio.gather(*tasks, return_exceptions=True)
        elapsed = time.perf_counter() - t_start

    latencies_ms.sort()
    success_count = len(latencies_ms)
    qps = success_count / elapsed if elapsed > 0 else 0.0
    avg_ms = (sum(latencies_ms) / success_count) if success_count > 0 else 0.0
    p95_ms = percentile(latencies_ms, 95.0)
    p99_ms = percentile(latencies_ms, 99.0)

    return {
        "concurrency": concurrency,
        "duration_sec": round(elapsed, 3),
        "requests_ok": success_count,
        "errors": errors,
        "qps": round(qps, 2),
        "avg_latency_ms": round(avg_ms, 2),
        "p95_latency_ms": round(p95_ms, 2),
        "p99_latency_ms": round(p99_ms, 2),
    }


async def main_async(args):
    results = []
    for c in args.concurrencies:
        print(f"[BENCH] Running concurrency={c} for {args.duration}s -> {args.url}")
        res = await run_level(args.url, c, args.duration, args.timeout)
        results.append(res)
        print(
            f"        QPS={res['qps']} | Avg={res['avg_latency_ms']}ms | "
            f"P95={res['p95_latency_ms']}ms | P99={res['p99_latency_ms']}ms | "
            f"OK={res['requests_ok']} Err={res['errors']}"
        )

    out_path = Path(args.output)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "target_url": args.url,
        "duration_per_level_sec": args.duration,
        "results": results,
    }
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"[DONE] Saved benchmark results to {out_path}")


def main():
    parser = argparse.ArgumentParser(description="Asyncio + aiohttp multi-concurrency benchmark")
    parser.add_argument("--url", default="http://127.0.0.1:8080/", help="Target URL to benchmark")
    parser.add_argument("--duration", type=float, default=3.0, help="Duration in seconds per concurrency level")
    parser.add_argument("--timeout", type=float, default=5.0, help="Per-request timeout in seconds")
    parser.add_argument(
        "--concurrencies",
        type=int,
        nargs="+",
        default=DEFAULT_CONCURRENCIES,
        help="Concurrency levels to test",
    )
    parser.add_argument(
        "--output",
        default=str(Path(__file__).resolve().parent / "results.json"),
        help="Output JSON file path",
    )
    args = parser.parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
