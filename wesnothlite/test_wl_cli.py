#!/usr/bin/env python3
"""
YAML-driven wl-cli integration test runner.

Usage:
    uv run --with pyyaml test_wl_cli.py <test.yaml>

YAML format:
    name: Human-readable test name
    campaign: CAMPAIGN_ID
    command_line_options: --rng-seed=42   # optional; forwarded after '--' to wl-cli
    sequence:
      - output: |          # strict: next N actual lines must match exactly
            [EVENT] scenario_start
      - input: units       # command sent to stdin
        output_match: |    # relaxed: each pattern line found in order, gaps allowed
            unit1 ...
      - input: quit        # input with no expected output
"""

import os
import sys
import subprocess
import re
from pathlib import Path

import yaml
import atexit

# --- Configuration & Paths ---
SCRIPT_DIR = Path(__file__).parent.resolve()
REPO_ROOT = SCRIPT_DIR.parents[1]
WL_CLI = REPO_ROOT / "wesnoth" / "build-debug" / "wl-cli"
WL_DATA = REPO_ROOT / "wesnoth" / "data"
WL_TIMEOUT = int(os.environ.get("WL_TIMEOUT", 180))


def matches(pattern: str, text: str, use_regex: bool = False) -> bool:
    """Literal match or regex match."""
    if use_regex:
        matcher = re.compile(pattern)
        return bool(matcher.findall(text))
    return pattern in text


def fail(msg: str, actual_lines: list[str], **extras) -> None:
    print(f"\n\n!!! FAIL: {msg} !!!")
    for key, val in extras.items():
        print(f"  {key}: {val}")
    print("\nFull actual output:")
    for i, line in enumerate(actual_lines):
        print(f"  {i+1:04d}: {line}")
    sys.exit(1)


def parse_pattern_lines(text: str) -> list[str]:
    """Split a YAML block scalar into non-empty stripped lines."""
    return [l.rstrip() for l in text.splitlines() if l.strip()]


def check(
    step_idx: int,
    patterns: list[str],
    actual: list[str],
    a_idx: int,
    use_regex: bool = False,
) -> int:
    """
    Strict match: consume exactly len(patterns) lines from actual starting at a_idx.
    Returns updated a_idx on success.
    """
    for i, pat in enumerate(patterns):
        if a_idx >= len(actual):
            fail(
                f"Step {step_idx}: actual output ended before all 'output' patterns matched",
                actual,
                missing_pattern=pat,
            )
        line = actual[a_idx]
        print(f"  [strict] expected='{pat}' actual='{line}' ", end="")
        if matches(pat, line, use_regex=use_regex):
            print("OK")
        else:
            print("MISMATCH")
            fail(
                f"Step {step_idx}: strict mismatch at pattern #{i+1}",
                actual,
                expected=pat,
                actual_line=line,
            )
        a_idx += 1
    return a_idx


def prepare_campaign(campaign: str) -> None:
    """Symlink the test campaign from the repo into WL_DATA, and register cleanup."""
    symlink_path = WL_DATA / "campaigns" / campaign
    test_campaign_dir = REPO_ROOT / "wesnoth" / "wesnothlite" / "tests" / campaign
    print(test_campaign_dir)

    if symlink_path.exists() or symlink_path.is_symlink():
        symlink_path.unlink()
    symlink_path.symlink_to(test_campaign_dir, target_is_directory=True)

    def cleanup_symlink():
        if symlink_path.exists() or symlink_path.is_symlink():
            symlink_path.unlink()

    atexit.register(cleanup_symlink)


def run_test(yaml_path: Path) -> None:
    with open(yaml_path) as f:
        cfg = yaml.safe_load(f)

    name = cfg.get("name", str(yaml_path))
    campaign = cfg["campaign"]
    options = cfg.get("command_line_options", "")
    sequence = cfg.get("sequence", [])

    print(f"=== {name} ===")

    if not WL_CLI.exists():
        print(f"ERROR: wl-cli not found at {WL_CLI}; build it first.")
        sys.exit(1)

    # Build command
    cmd = [str(WL_CLI), f"--data={WL_DATA}", f"--campaign={campaign}"]
    if options:
        cmd += ["--"] + options.split()

    # Prepare the environment
    prepare_campaign(campaign)

    # Build stdin from all input steps
    stdin_lines = [step["input"] for step in sequence if "input" in step]
    stdin_text = "\n".join(stdin_lines) + "\n"

    print(f"Command: {' '.join(cmd)}")
    print(f"Stdin ({len(stdin_lines)} commands):")
    for line in stdin_lines:
        print(f"  > {line}")
    print()

    try:
        proc = subprocess.run(
            cmd,
            input=stdin_text,
            capture_output=True,
            text=True,
            timeout=WL_TIMEOUT,
        )
        actual = proc.stdout.splitlines()
    except subprocess.TimeoutExpired as e:
        actual = (e.stdout or b"").decode().splitlines()
        print("WARNING: process timed out; checking partial output.")

    print(f"Actual output ({len(actual)} lines):")
    for i, line in enumerate(actual):
        print(f"  {i+1:04d}: {line}")
    print()

    # Walk sequence and validate
    a_idx = 0
    for step_idx, step in enumerate(sequence):
        if "output" in step or "output_match" in step:
            mode = "regex" if "output_match" in step else "strict"
            patterns = parse_pattern_lines(
                step["output_match" if mode == "regex" else "output"]
            )
            if patterns:
                print(f"Step {step_idx}: strict match ({len(patterns)} lines)")
                a_idx = check(
                    step_idx, patterns, actual, a_idx, use_regex=(mode == "regex")
                )

    print(
        f"\nPASS: all patterns verified ({a_idx} of {len(actual)} actual lines consumed)."
    )


def main() -> None:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <test.yaml>")
        sys.exit(1)

    yaml_path = Path(sys.argv[1])
    if not yaml_path.exists():
        print(f"ERROR: file not found: {yaml_path}")
        sys.exit(1)

    run_test(yaml_path)


if __name__ == "__main__":
    main()
