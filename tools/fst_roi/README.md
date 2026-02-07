# fst_roi

Simple CLI tool to print a region of interest (ROI) from an FST waveform. (VCD is not supported.)

## Dependencies

- Python 3.8+
- `pylibfst` (Python package, libfst bindings)

## Install

```bash
pip install pylibfst
```

## Usage

List signals:

```bash
./tools/fst_roi/fst_roi.py --fst path/to/wave.fst --list
```

Time-based ROI (LLM-friendly JSONL is the default):

```bash
./tools/fst_roi/fst_roi.py \
  --fst path/to/wave.fst \
  --signals TOP.clk,TOP.u0.state \
  --t0 1000 --t1 5000
```

Cycle-based ROI (rising edges of a clock):

```bash
./tools/fst_roi/fst_roi.py \
  --fst path/to/wave.fst \
  --signals TOP.u0.state \
  --clk TOP.clk \
  --cycle-start 10 --cycle-end 20
```

CSV output:

```bash
./tools/fst_roi/fst_roi.py \
  --fst path/to/wave.fst \
  --signals TOP.u0.state \
  --t0 1000 --t1 5000 \
  --format csv
```

LLM-friendly JSONL (grouped by time, plus one meta line):

```bash
./tools/fst_roi/fst_roi.py \
  --fst path/to/wave.fst \
  --signals TOP.u0.state \
  --t0 1000 --t1 5000 \
  --format jsonl
```

You can also use `--llm` as a shorthand for `--format jsonl`.
Add `--no-meta` to suppress the leading meta line.
Use `--jsonl-mode event` to emit one record per event (signal/time/value).
Use `--jsonl-mode fill` to emit snapshots with all signals filled from the last known value.

## Notes

- `--signals` supports glob patterns (e.g. `TOP.u0.*`).
- Time units are raw ticks from the waveform timescale.
- Use `--cycle-base 0` if your cycle numbering is 0-based.
