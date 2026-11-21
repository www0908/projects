#!/usr/bin/env python3
"""Generate an Apple-minimalist dual-subplot chart from benchmark results.json."""

import argparse
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def plot_benchmark(input_path: Path, output_path: Path):
    data = json.loads(input_path.read_text(encoding="utf-8"))
    rows = data["results"]

    labels = [str(r["concurrency"]) for r in rows]
    x = list(range(len(labels)))
    qps = [r["qps"] for r in rows]
    avg_lat = [r["avg_latency_ms"] for r in rows]
    p95_lat = [r["p95_latency_ms"] for r in rows]
    p99_lat = [r["p99_latency_ms"] for r in rows]

    plt.rcParams.update({
        "font.sans-serif": ["SF Pro Display", "Segoe UI", "PingFang SC", "DejaVu Sans", "Arial"],
        "axes.edgecolor": "#D2D2D7",
        "axes.linewidth": 0.9,
        "figure.facecolor": "#FFFFFF",
        "axes.facecolor": "#FFFFFF",
    })

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13.5, 5.4), dpi=300)
    fig.patch.set_facecolor("#FFFFFF")

    apple_blue = "#0071E3"
    space_gray = "#1D1D1F"
    slate_gray = "#86868B"
    grid_gray = "#E5E5EA"

    # --- Subplot 1: Throughput (QPS) ---
    bars = ax1.bar(x, qps, width=0.52, color=apple_blue, zorder=3)
    ax1.set_title("Throughput (QPS)", fontsize=14, fontweight="bold", color=space_gray, pad=14)
    ax1.set_xlabel("Concurrency", fontsize=11, color=slate_gray, labelpad=8)
    ax1.set_ylabel("Requests / sec", fontsize=11, color=slate_gray, labelpad=8)
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels, fontsize=10, color=space_gray)
    ax1.tick_params(axis="y", colors=slate_gray, labelsize=10)
    ax1.grid(axis="y", color=grid_gray, linestyle="-", linewidth=0.8, zorder=0)
    ax1.spines["top"].set_visible(False)
    ax1.spines["right"].set_visible(False)
    max_qps = max(qps) if qps else 1.0
    ax1.set_ylim(0, max_qps * 1.22 if max_qps > 0 else 10)

    for bar, val in zip(bars, qps):
        ax1.text(
            bar.get_x() + bar.get_width() / 2.0,
            bar.get_height() + max_qps * 0.025,
            f"{val:,.0f}",
            ha="center",
            va="bottom",
            fontsize=9.5,
            fontweight="semibold",
            color=space_gray,
        )

    # --- Subplot 2: Latency (Avg / P95 / P99) ---
    ax2.plot(x, avg_lat, marker="o", linewidth=2.2, markersize=6, color=apple_blue, label="Avg", zorder=4)
    ax2.plot(x, p95_lat, marker="s", linewidth=2.0, markersize=5.5, color=space_gray, label="P95", zorder=3)
    ax2.plot(x, p99_lat, marker="^", linewidth=2.0, markersize=6, linestyle="--", color=slate_gray, label="P99", zorder=2)

    ax2.set_title("Latency Distribution (ms)", fontsize=14, fontweight="bold", color=space_gray, pad=14)
    ax2.set_xlabel("Concurrency", fontsize=11, color=slate_gray, labelpad=8)
    ax2.set_ylabel("Latency (ms)", fontsize=11, color=slate_gray, labelpad=8)
    ax2.set_xticks(x)
    ax2.set_xticklabels(labels, fontsize=10, color=space_gray)
    ax2.tick_params(axis="y", colors=slate_gray, labelsize=10)
    ax2.grid(axis="y", color=grid_gray, linestyle="-", linewidth=0.8, zorder=0)
    ax2.spines["top"].set_visible(False)
    ax2.spines["right"].set_visible(False)

    max_lat = max(p99_lat + p95_lat + avg_lat) if rows else 1.0
    ax2.set_ylim(0, max_lat * 1.25 if max_lat > 0 else 10)

    for xi, a_val, p99_val in zip(x, avg_lat, p99_lat):
        if a_val > max_lat * 0.12:
            ax2.annotate(
                f"{a_val:.1f}",
                (xi, a_val),
                xytext=(0, -13),
                textcoords="offset points",
                ha="center",
                va="top",
                fontsize=8.5,
                color=apple_blue,
            )
        ax2.annotate(
            f"{p99_val:.1f}",
            (xi, p99_val),
            xytext=(0, 7),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=8.5,
            color=space_gray,
        )

    ax2.legend(frameon=False, loc="upper left", fontsize=10)

    fig.suptitle(
        "WebServer Multi-Concurrency Benchmark",
        fontsize=16,
        fontweight="bold",
        color=space_gray,
        y=0.98,
    )
    plt.tight_layout(rect=[0, 0, 1, 0.93])
    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_path, dpi=300, facecolor="#FFFFFF")
    plt.close(fig)
    print(f"[DONE] Saved chart to {output_path}")


def main():
    base = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Plot Apple-minimalist benchmark chart")
    parser.add_argument("--input", default=str(base / "results.json"), help="Input results.json path")
    parser.add_argument("--output", default=str(base / "benchmark_chart.png"), help="Output PNG path")
    args = parser.parse_args()
    plot_benchmark(Path(args.input), Path(args.output))


if __name__ == "__main__":
    main()
