# Repository Guidelines

## Project Structure & Module Organization
- `c/`: C99 implementation with `include/`, `src/`, `tests/`, and `examples/`; `make` builds static archives and runs unit tests.
- `cpp/`: C++23 implementation driven by CMake; headers in `cpp/include/`, tests in `cpp/tests/`, benchmarks in `cpp/benchmarks/`.
- `python/`: Pure-Python package; source in `python/zeroipc/`, tests in `python/tests/`, examples and benchmarks alongside.
- `docs/` holds MkDocs/Doxygen sources; `site/` is generated HTML. Top-level `Makefile` drives the unified CMake build into `build/`.
- `interop/` provides cross-language validation scripts; `whitepaper/` and `notebooks/` are research collateral—avoid touching unless necessary.

## Build, Test, and Development Commands
- Root CMake build: `cmake -B build . && cmake --build build` (Release by default via the Makefile), or simply `make build`.
- Full test sweep: `make test` (configures/uses `build/` then runs `ctest --output-on-failure -j$(nproc)`).
- Targeted CMake test: `ctest --test-dir build -R <name>` after a build.
- C library only: `cd c && make` / `make test` / `make examples`.
- Python: `cd python && pip install -e .[dev] && python -m pytest tests`.
- Docs check: `./test-docs.sh` before publishing docs changes.

## Coding Style & Naming Conventions
- C/C++: 4-space indent, keep existing brace placement; prefer clear, low-allocation code. Run `make format` (clang-format) before committing C/C++ changes.
- APIs and shared-memory object names follow snake_case identifiers (e.g., `"/sensor_data"`, `table_add`); C++ types stay in PascalCase (`Memory`, `Array`).
- Python: Black/isort (line length 88), Ruff for lint, optional mypy; keep modules snake_case and tests named `test_*.py`.
- Keep headers and sources minimal and deterministic; avoid new global state and ensure new structures match the binary spec in `SPECIFICATION.md`.

## Testing Guidelines
- Add or update tests alongside features: C/C++ in `cpp/tests/` (ctest) or `c/tests/` (make), Python in `python/tests/` (pytest).
- Name tests for behavior (`test_future_timeout`, `test_queue_backpressure`) and prefer deterministic, bounded-time cases.
- For interop work, run `interop/test_interop.sh` plus language-specific suites to catch spec drift.
- Aim for meaningful coverage on new branches/edge cases; use `python -m pytest --cov=zeroipc` when touching Python logic.

## Commit & Pull Request Guidelines
- Commit messages: imperative, <=72 chars, scoped when helpful (e.g., `cpp: harden latch wake-up`). Reference related issues in the body.
- PRs should describe intent, risks, and test evidence (`make test`, `python -m pytest`, interop scripts). Link issues, and include doc updates or CLI output snippets when behavior changes.
- Keep changes small and focused; note any performance impacts and how measured. Avoid rebasing force-pushes on shared branches unless coordinated.
