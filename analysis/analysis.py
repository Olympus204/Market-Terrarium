from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse
import os
import re
import sys
from typing import Iterable

import matplotlib.pyplot as plt
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RESULTS_DIR = ROOT / "results"
DEFAULT_FIGURES_DIR = ROOT / "figures"


@dataclass
class ResultFile:
    path: Path
    rows: int
    columns: list[str]
    experiment_name: str | None = None
    config_path: Path | None = None


@dataclass
class PlotSettings:
    x: str
    y: str
    plot_type: str = "scatter_median"
    aggregation: str = "median"
    group: str | None = None
    title: str | None = None
    x_label: str | None = None
    y_label: str | None = None


def clear_screen() -> None:
    if sys.stdout.isatty():
        print("\033[2J\033[H", end="")


def pause() -> None:
    input("\nPress Enter to continue...")


def prompt(text: str, default: str | None = None) -> str:
    if default is None:
        raw = input(f"{text}\n> ").strip()
        return raw

    raw = input(f"{text} [{default}]\n> ").strip()
    return raw if raw else default


def yes_no(text: str, default: bool = True) -> bool:
    default_text = "Y/n" if default else "y/N"

    while True:
        raw = input(f"{text} [{default_text}]\n> ").strip().lower()

        if not raw:
            return default
        if raw in {"y", "yes"}:
            return True
        if raw in {"n", "no"}:
            return False

        print("Please enter y or n.")


def read_config_name(csv_path: Path) -> tuple[str | None, Path | None]:
    config_path = csv_path.with_suffix(".cfg")

    if not config_path.exists():
        return None, None

    try:
        with config_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if line.lower().startswith("name="):
                    return line.split("=", 1)[1].strip(), config_path
    except OSError:
        pass

    return None, config_path


def inspect_result_file(path: Path) -> ResultFile | None:
    try:
        header = pd.read_csv(path, nrows=0)
        row_count = sum(1 for _ in path.open("r", encoding="utf-8", errors="ignore")) - 1
        row_count = max(row_count, 0)
    except Exception as error:
        print(f"Skipping {path.name}: {error}")
        return None

    experiment_name, config_path = read_config_name(path)

    return ResultFile(
        path=path,
        rows=row_count,
        columns=list(header.columns),
        experiment_name=experiment_name,
        config_path=config_path,
    )


def discover_result_files(results_dir: Path) -> list[ResultFile]:
    if not results_dir.exists():
        raise FileNotFoundError(f"Results directory does not exist: {results_dir}")

    candidates = sorted(
        results_dir.glob("*.csv"),
        key=lambda p: p.stat().st_mtime,
        reverse=True,
    )

    files: list[ResultFile] = []

    for path in candidates:
        info = inspect_result_file(path)
        if info is not None:
            files.append(info)

    return files


def format_file_table(files: list[ResultFile]) -> None:
    if not files:
        print("No CSV files found.")
        return

    print("Available result files\n")
    print(f"{'#':>3}  {'Rows':>7}  {'Experiment':<24}  File")
    print("-" * 100)

    for index, info in enumerate(files, start=1):
        experiment = info.experiment_name or "-"
        if len(experiment) > 24:
            experiment = experiment[:21] + "..."

        print(
            f"{index:>3}  "
            f"{info.rows:>7}  "
            f"{experiment:<24}  "
            f"{info.path.name}"
        )


def parse_selection(text: str, count: int) -> list[int]:
    text = text.strip().lower()

    if text == "all":
        return list(range(count))

    selected: list[int] = []

    for token in text.split(","):
        token = token.strip()
        if not token:
            continue

        if "-" in token:
            left, right = token.split("-", 1)
            start = int(left)
            end = int(right)

            if start > end:
                start, end = end, start

            for value in range(start, end + 1):
                if value < 1 or value > count:
                    raise ValueError(f"Selection {value} is out of range.")
                selected.append(value - 1)
        else:
            value = int(token)
            if value < 1 or value > count:
                raise ValueError(f"Selection {value} is out of range.")
            selected.append(value - 1)

    if not selected:
        raise ValueError("No files selected.")

    # preserve order while removing duplicates
    return list(dict.fromkeys(selected))


def choose_files(files: list[ResultFile]) -> list[ResultFile]:
    while True:
        clear_screen()
        format_file_table(files)
        print("\nChoose one or more files with commas/ranges, or 'all'.")
        print("Examples: 1   1,3,5   1-4   all")

        raw = input("> ").strip()

        try:
            indices = parse_selection(raw, len(files))
            return [files[i] for i in indices]
        except Exception as error:
            print(f"\n{error}")
            pause()


def load_files(files: list[ResultFile]) -> pd.DataFrame:
    frames: list[pd.DataFrame] = []

    for info in files:
        frame = pd.read_csv(info.path)
        frame["source_file"] = info.path.name
        frame["source_experiment"] = info.experiment_name or info.path.stem
        frames.append(frame)

    if not frames:
        raise ValueError("No data was loaded.")

    return pd.concat(frames, ignore_index=True, sort=False)


def shared_columns(files: list[ResultFile]) -> list[str]:
    if not files:
        return []

    shared = set(files[0].columns)

    for info in files[1:]:
        shared &= set(info.columns)

    ordered = [
        column
        for column in files[0].columns
        if column in shared
    ]

    ordered.extend(["source_file", "source_experiment"])

    return ordered


def numeric_columns(df: pd.DataFrame, columns: Iterable[str]) -> list[str]:
    result: list[str] = []

    for column in columns:
        if column not in df.columns:
            continue

        series = pd.to_numeric(df[column], errors="coerce")
        if series.notna().any():
            result.append(column)

    return result


def choose_column(
    columns: list[str],
    label: str,
    current: str | None = None,
) -> str:
    while True:
        clear_screen()
        print(f"{label}\n")

        for index, column in enumerate(columns, start=1):
            marker = "  *" if column == current else ""
            print(f"[{index:>3}] {column}{marker}")

        default = None
        if current in columns:
            default = str(columns.index(current) + 1)

        raw = prompt("Choose a column by number or exact name", default)

        if raw in columns:
            return raw

        try:
            index = int(raw)
            if 1 <= index <= len(columns):
                return columns[index - 1]
        except ValueError:
            pass

        print("That column does not exist.")
        pause()


def choose_optional_column(
    columns: list[str],
    label: str,
    current: str | None = None,
) -> str | None:
    while True:
        clear_screen()
        print(f"{label}\n")
        print("[0] None")

        for index, column in enumerate(columns, start=1):
            marker = "  *" if column == current else ""
            print(f"[{index:>3}] {column}{marker}")

        default = "0"
        if current in columns:
            default = str(columns.index(current) + 1)

        raw = prompt("Choose a column", default)

        if raw.lower() in {"0", "none", "off"}:
            return None

        if raw in columns:
            return raw

        try:
            index = int(raw)
            if 1 <= index <= len(columns):
                return columns[index - 1]
        except ValueError:
            pass

        print("That column does not exist.")
        pause()


def choose_plot_type(current: str = "scatter_median") -> str:
    options = [
        ("scatter_median", "Raw scatter + aggregated trend"),
        ("scatter", "Raw scatter"),
        ("aggregate_line", "Aggregated line"),
        ("line", "Raw line"),
        ("bar", "Aggregated bar"),
    ]

    while True:
        clear_screen()
        print("Plot type\n")

        for index, (key, description) in enumerate(options, start=1):
            marker = "  *" if key == current else ""
            print(f"[{index}] {description}{marker}")

        current_index = next(
            i
            for i, (key, _) in enumerate(options, start=1)
            if key == current
        )

        raw = prompt("Choose plot type", str(current_index))

        try:
            index = int(raw)
            if 1 <= index <= len(options):
                return options[index - 1][0]
        except ValueError:
            pass

        keys = {key for key, _ in options}
        if raw in keys:
            return raw

        print("Invalid plot type.")
        pause()


def choose_aggregation(current: str = "median") -> str:
    options = ["median", "mean", "min", "max"]

    while True:
        clear_screen()
        print("Aggregation\n")

        for index, option in enumerate(options, start=1):
            marker = "  *" if option == current else ""
            print(f"[{index}] {option}{marker}")

        current_index = options.index(current) + 1
        raw = prompt("Choose aggregation", str(current_index))

        if raw in options:
            return raw

        try:
            index = int(raw)
            if 1 <= index <= len(options):
                return options[index - 1]
        except ValueError:
            pass

        print("Invalid aggregation.")
        pause()


OPERATORS = {
    "==": lambda series, value: series == value,
    "!=": lambda series, value: series != value,
    ">": lambda series, value: series > value,
    "<": lambda series, value: series < value,
    ">=": lambda series, value: series >= value,
    "<=": lambda series, value: series <= value,
}


def parse_filter_value(series: pd.Series, raw: str):
    numeric = pd.to_numeric(series, errors="coerce")

    if numeric.notna().sum() >= max(1, int(0.8 * series.notna().sum())):
        try:
            return float(raw), numeric
        except ValueError:
            raise ValueError(
                f"{series.name} is numeric, so filter value must be numeric."
            )

    return raw, series.astype(str)


def apply_filter_expression(
    df: pd.DataFrame,
    expression: str,
) -> pd.DataFrame:
    pattern = re.compile(
        r"^\s*(?P<column>.+?)\s*(?P<op>>=|<=|==|!=|>|<)\s*(?P<value>.+?)\s*$"
    )
    match = pattern.match(expression)

    if not match:
        raise ValueError(
            "Filter must look like: neural_count == 40"
        )

    column = match.group("column").strip()
    operator = match.group("op")
    raw_value = match.group("value").strip()

    if column not in df.columns:
        raise ValueError(f"Unknown column: {column}")

    value, comparable = parse_filter_value(df[column], raw_value)

    mask = OPERATORS[operator](comparable, value)
    mask = mask.fillna(False)

    return df.loc[mask].copy()


def add_filter(
    df: pd.DataFrame,
    filters: list[str],
) -> tuple[pd.DataFrame, list[str]]:
    clear_screen()
    print("Add filter\n")
    print("Examples:")
    print("  neural_count == 40")
    print("  random_count >= 80")
    print("  source_file == my_results.csv")
    print("\nUse one comparison per filter. Multiple filters are ANDed.")

    expression = input("> ").strip()

    if not expression:
        return df, filters

    filtered = apply_filter_expression(df, expression)

    if filtered.empty:
        print("\nThat filter would leave zero rows.")
        pause()
        return df, filters

    filters = [*filters, expression]
    return filtered, filters


def aggregate_series(
    df: pd.DataFrame,
    x: str,
    y: str,
    aggregation: str,
) -> pd.DataFrame:
    working = df[[x, y]].copy()
    working[y] = pd.to_numeric(working[y], errors="coerce")
    working = working.dropna(subset=[x, y])

    grouped = (
        working
        .groupby(x, dropna=False)[y]
        .agg(aggregation)
        .reset_index()
    )

    try:
        grouped = grouped.sort_values(x)
    except TypeError:
        grouped[x] = grouped[x].astype(str)
        grouped = grouped.sort_values(x)

    return grouped


def grouped_frames(
    df: pd.DataFrame,
    group: str | None,
) -> list[tuple[str | None, pd.DataFrame]]:
    if group is None:
        return [(None, df)]

    values = list(df[group].dropna().unique())

    try:
        values = sorted(values)
    except TypeError:
        values = sorted(values, key=str)

    return [
        (str(value), df[df[group] == value].copy())
        for value in values
    ]


def make_plot(
    df: pd.DataFrame,
    settings: PlotSettings,
) -> tuple[plt.Figure, plt.Axes]:
    if settings.x not in df.columns or settings.y not in df.columns:
        raise ValueError("Selected columns are not present in the data.")

    working = df.copy()
    working[settings.y] = pd.to_numeric(
        working[settings.y],
        errors="coerce",
    )
    working = working.dropna(subset=[settings.x, settings.y])

    if working.empty:
        raise ValueError("No plottable rows remain after filtering.")

    fig, ax = plt.subplots(figsize=(10, 6))

    groups = grouped_frames(working, settings.group)

    for label, frame in groups:
        legend_label = label if settings.group else None

        if settings.plot_type in {"scatter", "scatter_median"}:
            x_numeric = pd.to_numeric(frame[settings.x], errors="coerce")

            if x_numeric.notna().all():
                x_values = x_numeric
            else:
                x_values = frame[settings.x].astype(str)

            ax.scatter(
                x_values,
                frame[settings.y],
                alpha=0.5,
                label=legend_label,
            )

        if settings.plot_type in {"aggregate_line", "scatter_median"}:
            aggregated = aggregate_series(
                frame,
                settings.x,
                settings.y,
                settings.aggregation,
            )

            x_numeric = pd.to_numeric(
                aggregated[settings.x],
                errors="coerce",
            )

            if x_numeric.notna().all():
                x_values = x_numeric
            else:
                x_values = aggregated[settings.x].astype(str)

            line_label = legend_label
            if settings.group is None:
                line_label = settings.aggregation

            ax.plot(
                x_values,
                aggregated[settings.y],
                marker="o",
                linewidth=2,
                label=line_label,
            )

        elif settings.plot_type == "line":
            frame = frame.copy()

            try:
                frame = frame.sort_values(settings.x)
            except TypeError:
                frame[settings.x] = frame[settings.x].astype(str)
                frame = frame.sort_values(settings.x)

            x_numeric = pd.to_numeric(frame[settings.x], errors="coerce")

            if x_numeric.notna().all():
                x_values = x_numeric
            else:
                x_values = frame[settings.x].astype(str)

            ax.plot(
                x_values,
                frame[settings.y],
                marker="o",
                linewidth=1.5,
                label=legend_label,
            )

        elif settings.plot_type == "bar":
            aggregated = aggregate_series(
                frame,
                settings.x,
                settings.y,
                settings.aggregation,
            )

            x_labels = aggregated[settings.x].astype(str)

            if settings.group is None:
                ax.bar(
                    x_labels,
                    aggregated[settings.y],
                    label=settings.aggregation,
                )
            else:
                # Grouped bars are laid out with a small offset.
                all_x = list(dict.fromkeys(
                    working[settings.x].astype(str)
                ))
                positions = {value: i for i, value in enumerate(all_x)}

                group_count = max(len(groups), 1)
                group_index = [
                    name for name, _ in groups
                ].index(label)

                width = 0.8 / group_count
                offset = (
                    group_index - (group_count - 1) / 2
                ) * width

                numeric_positions = [
                    positions[value] + offset
                    for value in x_labels
                ]

                ax.bar(
                    numeric_positions,
                    aggregated[settings.y],
                    width=width,
                    label=legend_label,
                )
                ax.set_xticks(range(len(all_x)))
                ax.set_xticklabels(all_x)

    ax.grid(True, alpha=0.25)
    ax.set_axisbelow(True)

    ax.set_xlabel(settings.x_label or settings.x)
    ax.set_ylabel(settings.y_label or settings.y)

    title = settings.title
    if not title:
        title = f"{settings.y} vs {settings.x}"

        if settings.plot_type in {
            "aggregate_line",
            "scatter_median",
            "bar",
        }:
            title += f" ({settings.aggregation})"

    ax.set_title(title)

    if settings.group is not None or settings.plot_type in {
        "aggregate_line",
        "scatter_median",
    }:
        ax.legend(title=settings.group)

    fig.tight_layout()
    return fig, ax


def safe_filename(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_-]+", "_", value.strip())
    value = value.strip("_")
    return value or "plot"


def save_plot(
    fig: plt.Figure,
    settings: PlotSettings,
    figures_dir: Path,
) -> Path:
    figures_dir.mkdir(parents=True, exist_ok=True)

    name = safe_filename(
        f"{settings.y}_vs_{settings.x}"
        + (
            f"_by_{settings.group}"
            if settings.group
            else ""
        )
    )

    path = figures_dir / f"{name}.png"

    counter = 2
    while path.exists():
        path = figures_dir / f"{name}_{counter}.png"
        counter += 1

    fig.savefig(
        path,
        dpi=300,
        bbox_inches="tight",
    )

    return path


def display_available() -> bool:
    if sys.platform.startswith("win"):
        return True
    if sys.platform == "darwin":
        return True

    return bool(
        os.environ.get("DISPLAY") or
        os.environ.get("WAYLAND_DISPLAY")
    )


def show_or_save_plot(
    df: pd.DataFrame,
    settings: PlotSettings,
    figures_dir: Path,
    force_no_show: bool,
) -> None:
    try:
        fig, _ = make_plot(df, settings)
    except Exception as error:
        print(f"\nCould not create graph: {error}")
        pause()
        return

    can_show = display_available() and not force_no_show

    if can_show:
        plt.show(block=False)
        plt.pause(0.1)
    else:
        print(
            "\nNo graphical display detected. "
            "The plot can still be saved as a PNG."
        )

    if yes_no("Save graph as PNG?", default=not can_show):
        path = save_plot(fig, settings, figures_dir)
        print(f"Saved: {path}")

    if can_show:
        print(
            "\nThe graph window will update the next time "
            "you redraw the plot."
        )

    pause()
    plt.close(fig)


def print_state(
    original_df: pd.DataFrame,
    filtered_df: pd.DataFrame,
    selected_files: list[ResultFile],
    settings: PlotSettings,
    filters: list[str],
) -> None:
    print("Current analysis\n")
    print(f"Files: {len(selected_files)}")
    print(f"Rows loaded: {len(original_df):,}")
    print(f"Rows after filters: {len(filtered_df):,}")
    print(f"X: {settings.x}")
    print(f"Y: {settings.y}")
    print(f"Plot: {settings.plot_type}")
    print(f"Aggregation: {settings.aggregation}")
    print(f"Group: {settings.group or 'none'}")
    print(f"Filters: {len(filters)}")

    if filters:
        for item in filters:
            print(f"  - {item}")


def choose_initial_settings(
    df: pd.DataFrame,
    columns: list[str],
) -> PlotSettings:
    numerics = numeric_columns(df, columns)

    if len(numerics) < 2:
        raise ValueError(
            "The selected files do not share at least two numeric columns."
        )

    x = choose_column(numerics, "Choose X axis")
    y = choose_column(numerics, "Choose Y axis")

    return PlotSettings(
        x=x,
        y=y,
        plot_type="scatter_median",
        aggregation="median",
    )


def analysis_loop(
    all_files: list[ResultFile],
    selected_files: list[ResultFile],
    figures_dir: Path,
    force_no_show: bool,
) -> None:
    original_df = load_files(selected_files)
    common = shared_columns(selected_files)

    settings = choose_initial_settings(original_df, common)

    filters: list[str] = []
    filtered_df = original_df.copy()

    while True:
        clear_screen()
        print_state(
            original_df,
            filtered_df,
            selected_files,
            settings,
            filters,
        )

        print("\nGraph options\n")
        print("[1] Draw graph")
        print("[2] Change X column")
        print("[3] Change Y column")
        print("[4] Change both columns")
        print("[5] Change plot type")
        print("[6] Change aggregation")
        print("[7] Add/change grouping column")
        print("[8] Add filter")
        print("[9] Clear filters")
        print("[10] Choose different files")
        print("[11] Set custom title / axis labels")
        print("[0] Exit")

        choice = input("> ").strip()

        numeric_common = numeric_columns(filtered_df, common)

        if choice == "1":
            show_or_save_plot(
                filtered_df,
                settings,
                figures_dir,
                force_no_show,
            )

        elif choice == "2":
            settings.x = choose_column(
                numeric_common,
                "Choose X axis",
                settings.x,
            )

        elif choice == "3":
            settings.y = choose_column(
                numeric_common,
                "Choose Y axis",
                settings.y,
            )

        elif choice == "4":
            settings.x = choose_column(
                numeric_common,
                "Choose X axis",
                settings.x,
            )
            settings.y = choose_column(
                numeric_common,
                "Choose Y axis",
                settings.y,
            )

        elif choice == "5":
            settings.plot_type = choose_plot_type(
                settings.plot_type
            )

        elif choice == "6":
            settings.aggregation = choose_aggregation(
                settings.aggregation
            )

        elif choice == "7":
            settings.group = choose_optional_column(
                common,
                "Choose grouping column",
                settings.group,
            )

        elif choice == "8":
            try:
                filtered_df, filters = add_filter(
                    filtered_df,
                    filters,
                )
            except Exception as error:
                print(f"\nCould not apply filter: {error}")
                pause()

        elif choice == "9":
            filters = []
            filtered_df = original_df.copy()

        elif choice == "10":
            selected_files = choose_files(all_files)
            original_df = load_files(selected_files)
            filtered_df = original_df.copy()
            common = shared_columns(selected_files)
            filters = []
            settings = choose_initial_settings(
                original_df,
                common,
            )

        elif choice == "11":
            clear_screen()
            settings.title = prompt(
                "Graph title (blank means automatic)",
                settings.title or "",
            ) or None
            settings.x_label = prompt(
                "X-axis label (blank means column name)",
                settings.x_label or "",
            ) or None
            settings.y_label = prompt(
                "Y-axis label (blank means column name)",
                settings.y_label or "",
            ) or None

        elif choice == "0":
            return

        else:
            print("Invalid option.")
            pause()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Interactive Market Terrarium CSV explorer."
    )
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=DEFAULT_RESULTS_DIR,
        help="Directory containing result CSV files.",
    )
    parser.add_argument(
        "--figures-dir",
        type=Path,
        default=DEFAULT_FIGURES_DIR,
        help="Directory where saved PNG plots are written.",
    )
    parser.add_argument(
        "--no-show",
        action="store_true",
        help="Never open a matplotlib window; useful over SSH.",
    )

    args = parser.parse_args()

    try:
        files = discover_result_files(args.results_dir)

        if not files:
            print(f"No CSV files found in {args.results_dir}")
            return 1

        selected = choose_files(files)

        analysis_loop(
            files,
            selected,
            args.figures_dir,
            args.no_show,
        )

        return 0

    except KeyboardInterrupt:
        print("\nCancelled.")
        return 130
    except Exception as error:
        print(f"Analysis error: {error}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
