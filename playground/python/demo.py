from __future__ import annotations

import argparse
import pathlib
import sys


def load_module(module_dir: str):
    sys.path.insert(0, str(pathlib.Path(module_dir).resolve()))
    import nix_playground  # pylint: disable=import-error

    return nix_playground


def main() -> int:
    thread_count = 4
    increments_per_thread = 2500

    parser = argparse.ArgumentParser(description="Run the pybind11 playground demo.")
    parser.add_argument("--module-dir", required=True, help="Directory containing nix_playground.")
    parser.add_argument("--chunks", type=int, default=6)
    parser.add_argument("--delay-ms", type=int, default=20)
    args = parser.parse_args()

    nix_playground = load_module(args.module_dir)
    transfer = nix_playground.simulate_transfer(args.chunks, args.delay_ms)
    total = nix_playground.parallel_increment(thread_count, increments_per_thread)
    expected_total = thread_count * increments_per_thread

    print("simulate_transfer:", dict(transfer))
    print("parallel_increment:", total)
    return 0 if total == expected_total else 1


if __name__ == "__main__":
    raise SystemExit(main())
