from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

ROOT = Path(__file__).resolve().parents[1]

files = {
    "45 traders": {
            "path": ROOT / "results" / "random_transition_2026-09-24_06-52-47.csv",
            "total_traders": 45,
        },

    "90 traders": {
        "path": ROOT / "results" / "random_transition_2026-09-23_17-38-42.csv",
        "total_traders": 90,
    },

    "135 traders": {
            "path": ROOT / "results" / "strategy_transition_2026-09-27_18-14-22.csv",
            "total_traders": 135,
            "experiment_type": "random_rebalancer"
        },

    "180 traders": {
        "path": ROOT / "results" / "strategy_transition_2026-09-24_17-31-00.csv",
        "total_traders": 180,
        "experiment_type": "random_rebalancer"
    },
}
summaries = []

for label, info in files.items():
    path = info["path"]

    df = pd.read_csv(path)
    df = df[df["mean_count"] == 0].copy()

    if info.get("experiment_type") is not None:
        df = df[
            df["experiment_type"] == info["experiment_type"]
        ].copy()

    df["persistent"] = (
        df["last_trade_tick"]
        >= df["ticks_requested"] - 1000
    )

    summary = (
        df.groupby("random_count")
          .agg(
              persistence=("persistent", "mean"),
              runs=("seed", "count"),
              mean_trades=("total_trades", "mean"),
              median_last_trade=("last_trade_tick", "median")
          )
          .reset_index()
    )

    summary["persistence"] *= 100

    summary["random_percent"] = (
        summary["random_count"]
        / info["total_traders"]
        * 100
    )

    summary["experiment"] = label

    summaries.append(summary)

combined = pd.concat(
    summaries,
    ignore_index=True
)

fig, ax = plt.subplots(figsize=(10, 6))

sns.lineplot(
    data=combined,
    x="random_count",
    y="persistence",
    hue="experiment",
    marker="o",
    linewidth=2,
    markersize=7,
    ax=ax
)

ax.grid(axis="y", linestyle="--", alpha=0.3)
ax.grid(axis="x", linestyle="--", alpha=0.3)
ax.set_axisbelow(True)

ax.set_title(
    "How many random traders are needed for persistent trading?",
    fontsize=16
)

ax.set_xlabel("Number of random traders")
ax.set_ylabel("Runs trading in final 1,000 ticks (%)")

ax.set_xlim(0, 10)
ax.set_ylim(0, 100)
ax.set_xticks(range(0, 11))

ax.axhline(
    50,
    linestyle="--",
    linewidth=1,
    alpha=0.5
)

ax.text(
    9.8,
    51.5,
    "50% persistence",
    ha="right",
    va="bottom"
)

ax.legend(title="Total traders")

plt.tight_layout()
plt.savefig(
    "persistent_trading_transition.png",
    dpi=300,
    bbox_inches="tight"
)
ax.set_ylim(0, 100)
ax.set_xlim(0, 10)
ax.set_xticks(range(0, 11))

ax.axhline(
    50,
    linestyle="--",
    linewidth=1,
    alpha=0.5
)

plt.tight_layout()

figure_directory = ROOT / "figures"
figure_directory.mkdir(exist_ok=True)

fig.savefig(
    figure_directory / "random_trader_persistence.png",
    dpi=300,
    bbox_inches="tight"
)

plt.show()