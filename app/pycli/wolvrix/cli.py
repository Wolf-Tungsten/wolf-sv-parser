from __future__ import annotations

import argparse
import sys

from . import Design, from_json_string, list_passes, read_json, read_sv


def _load_design(input_path: str | None) -> Design:
    if input_path:
        return read_json(input_path)
    data = sys.stdin.read()
    if not data.strip():
        raise SystemExit("expected JSON input on stdin (or use --in)")
    return from_json_string(data)


def _write_json_output(design: Design, output_path: str | None, mode: str, top: list[str]) -> None:
    text = design.to_json(mode=mode, top=top)
    if output_path:
        with open(output_path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return
    sys.stdout.write(text)
    if not text.endswith("\n"):
        sys.stdout.write("\n")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="wolvrix",
        description="Wolvrix Python CLI (binding-based)",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    read_sv_parser = subparsers.add_parser("read-sv", help="Read SV and output GRH JSON")
    read_sv_parser.add_argument("file", help="SystemVerilog file")
    read_sv_parser.add_argument("--out", "-o", dest="output", help="Output JSON path (default: stdout)")
    read_sv_parser.add_argument(
        "--log-level",
        default="warn",
        choices=["trace", "debug", "info", "warn", "error", "off"],
        help="Logging verbosity for convert",
    )
    read_sv_parser.add_argument(
        "slang_args",
        nargs=argparse.REMAINDER,
        help="Arguments after -- are passed to slang",
    )

    transform_parser = subparsers.add_parser("transform", help="Run a transform pass on GRH JSON")
    transform_parser.add_argument("pass_name", help="Transform pass name")
    transform_parser.add_argument("--in", dest="input", help="Input JSON path (default: stdin)")
    transform_parser.add_argument("--out", "-o", dest="output", help="Output JSON path (default: stdout)")
    transform_parser.add_argument("--dryrun", action="store_true", help="Run pass without mutating design")
    transform_parser.add_argument(
        "pass_args",
        nargs=argparse.REMAINDER,
        help="Arguments after -- are passed to the pass",
    )

    write_sv_parser = subparsers.add_parser("write-sv", help="Emit SV from GRH JSON")
    write_sv_parser.add_argument("--in", dest="input", help="Input JSON path (default: stdin)")
    write_sv_parser.add_argument("--out", "-o", dest="output", required=True, help="Output SV path")
    write_sv_parser.add_argument("--top", action="append", default=[], help="Top graph override")

    write_json_parser = subparsers.add_parser("write-json", help="Write GRH JSON (optionally reformatted)")
    write_json_parser.add_argument("--in", dest="input", help="Input JSON path (default: stdin)")
    write_json_parser.add_argument("--out", "-o", dest="output", help="Output JSON path (default: stdout)")
    write_json_parser.add_argument(
        "--mode",
        default="pretty-compact",
        choices=["compact", "pretty-compact", "pretty"],
        help="JSON formatting mode",
    )
    write_json_parser.add_argument("--top", action="append", default=[], help="Top graph override")

    subparsers.add_parser("pass-list", help="List available transform passes")

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        if args.command == "read-sv":
            slang_args = args.slang_args
            if slang_args and slang_args[0] == "--":
                slang_args = slang_args[1:]
            design = read_sv(args.file, slang_args=slang_args, log_level=args.log_level)
            _write_json_output(design, args.output, mode="pretty-compact", top=[])
            return 0

        if args.command == "transform":
            pass_args = args.pass_args
            if pass_args and pass_args[0] == "--":
                pass_args = pass_args[1:]
            design = _load_design(args.input)
            design.run_pass(args.pass_name, pass_args, dryrun=args.dryrun)
            _write_json_output(design, args.output, mode="pretty-compact", top=[])
            return 0

        if args.command == "write-sv":
            design = _load_design(args.input)
            design.write_sv(args.output, top=args.top)
            return 0

        if args.command == "write-json":
            design = _load_design(args.input)
            _write_json_output(design, args.output, mode=args.mode, top=args.top)
            return 0

        if args.command == "pass-list":
            for name in list_passes():
                print(name)
            return 0
    except Exception as exc:  # pragma: no cover - CLI guard
        print(str(exc), file=sys.stderr)
        return 1

    parser.error(f"unknown command: {args.command}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
