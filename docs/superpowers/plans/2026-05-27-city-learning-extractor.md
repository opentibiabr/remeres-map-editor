# City Learning Extractor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert large `rme-city-corpus` v2 files into compact statistical, building-template and semantic-layout training artifacts.

**Architecture:** A Python CLI reads corpus JSON incrementally through `ijson`, passing house and tile events into focused accumulators. The accumulators generate a compact JSON model plus gzip JSONL streams for house geometry and RLE semantic layouts, preserving spatial patterns without loading the raw corpus into memory.

**Tech Stack:** Python 3.11, `ijson`, standard-library `gzip`, `json`, `unittest`, `collections`.

---

### Task 1: Establish Tested Semantic Accumulators

**Files:**
- Create: `tools/city_learning/__init__.py`
- Create: `tools/city_learning/model.py`
- Create: `tests/city_learning/test_model.py`

- [ ] Write failing tests for deterministic semantic signatures, row RLE and
  anchor-house footprint extraction.
- [ ] Run `python -m unittest tests.city_learning.test_model -v` and observe
  missing-module/test failures before implementation.
- [ ] Implement focused accumulators for tile tags, layouts, house footprints
  and ranked counters.
- [ ] Run the unit tests until they pass.

### Task 2: Stream Corpus V2 Into Artifacts

**Files:**
- Create: `tools/city_learning/extract_city_learning.py`
- Create: `tools/city_learning/requirements.txt`
- Create: `tests/city_learning/test_extract_city_learning.py`

- [ ] Write a small v2 corpus fixture in the integration test and assert the
  model, building gzip stream and layout gzip stream outputs.
- [ ] Run `python -m unittest tests.city_learning.test_extract_city_learning -v`
  and verify it fails because the CLI reader is not implemented.
- [ ] Implement an `ijson.parse` state reader that constructs only current
  house/tile records and finalizes one district at a time.
- [ ] Add CLI argument parsing, deterministic output writing and dependency
  error reporting.
- [ ] Install `ijson` from the declared requirements and run all tests.

### Task 3: Process And Inspect The Real Corpus

**Files:**
- Generate outside repository:
  `D:/kl/canary/data-otservbr-global/world/city-learning/city-learning-model.json`
  `D:/kl/canary/data-otservbr-global/world/city-learning/building-templates.jsonl.gz`
  `D:/kl/canary/data-otservbr-global/world/city-learning/semantic-layouts.jsonl.gz`

- [ ] Run the extractor against
  `D:/kl/canary/data-otservbr-global/world/all-towns-city-corpus.json`.
- [ ] Parse the generated compact model and verify 30 cities, 58 districts
  and a nonzero prototype catalog.
- [ ] Report file sizes and counts so the next generator phase is based on
  evidence rather than raw corpus assumptions.

### Task 4: Version And Verify The Feature

**Files:**
- Commit only: `tools/city_learning/**`, `tests/city_learning/**`, and the two
  documentation files for this feature.

- [ ] Run `git diff --check`.
- [ ] Run `python -m unittest discover -s tests -p "test_*.py" -v`.
- [ ] Inspect `git status --short --branch` and do not stage unrelated
  `change_build_style*` or SQLite runtime files.
- [ ] Commit the completed extractor with a conventional feature message.
