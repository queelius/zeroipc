# Makefile for ZeroIPC Python package
SHELL := /bin/bash
PYTHON := python3
VENV := .venv
VENV_PYTHON := $(VENV)/bin/python
VENV_PIP := $(VENV)/bin/pip
PROJECT_NAME := zeroipc

# Colors for output
BLUE := \033[0;34m
GREEN := \033[0;32m
RED := \033[0;31m
NC := \033[0m # No Color

.PHONY: help
help: ## Show this help message
	@echo "$(BLUE)ZeroIPC Python Package Management$(NC)"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "  $(GREEN)%-15s$(NC) %s\n", $$1, $$2}'

.PHONY: venv
venv: ## Create virtual environment
	@if [ ! -d $(VENV) ]; then \
		echo "$(BLUE)Creating virtual environment...$(NC)"; \
		$(PYTHON) -m venv $(VENV); \
		$(VENV_PIP) install --upgrade pip setuptools wheel; \
		echo "$(GREEN)Virtual environment created$(NC)"; \
	else \
		echo "$(GREEN)Virtual environment already exists$(NC)"; \
	fi

.PHONY: install
install: venv ## Install package in development mode
	@echo "$(BLUE)Installing package in development mode...$(NC)"
	$(VENV_PIP) install -e .
	@echo "$(GREEN)Package installed$(NC)"

.PHONY: install-dev
install-dev: install ## Install development dependencies
	@echo "$(BLUE)Installing development dependencies...$(NC)"
	$(VENV_PIP) install pytest pytest-cov pytest-xdist black isort mypy ruff
	@echo "$(GREEN)Development dependencies installed$(NC)"

.PHONY: test
test: ## Run tests
	@echo "$(BLUE)Running tests...$(NC)"
	$(VENV)/bin/pytest tests/ -v

.PHONY: test-cov
test-cov: ## Run tests with coverage
	@echo "$(BLUE)Running tests with coverage...$(NC)"
	$(VENV)/bin/pytest tests/ --cov=zeroipc --cov-report=term-missing --cov-report=html

.PHONY: test-parallel
test-parallel: ## Run tests in parallel
	@echo "$(BLUE)Running tests in parallel...$(NC)"
	$(VENV)/bin/pytest tests/ -n auto

.PHONY: format
format: ## Format code with black and isort
	@echo "$(BLUE)Formatting code...$(NC)"
	$(VENV)/bin/isort zeroipc/ tests/
	$(VENV)/bin/black zeroipc/ tests/
	@echo "$(GREEN)Code formatted$(NC)"

.PHONY: lint
lint: ## Run linting checks
	@echo "$(BLUE)Running linting checks...$(NC)"
	$(VENV)/bin/ruff check zeroipc/ tests/
	$(VENV)/bin/mypy zeroipc/ --ignore-missing-imports

.PHONY: check
check: lint test ## Run all checks (lint + test)
	@echo "$(GREEN)All checks passed$(NC)"

.PHONY: clean
clean: ## Clean build artifacts and cache
	@echo "$(BLUE)Cleaning build artifacts...$(NC)"
	rm -rf build/ dist/ *.egg-info
	rm -rf .pytest_cache/ .coverage htmlcov/
	rm -rf **/__pycache__ **/*.pyc
	find . -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
	find . -type f -name "*.pyc" -delete
	@echo "$(GREEN)Cleaned$(NC)"

.PHONY: clean-all
clean-all: clean ## Clean everything including venv
	@echo "$(BLUE)Removing virtual environment...$(NC)"
	rm -rf $(VENV)
	@echo "$(GREEN)All cleaned$(NC)"

.PHONY: build
build: clean ## Build distribution packages
	@echo "$(BLUE)Building distribution packages...$(NC)"
	$(VENV_PYTHON) -m pip install --upgrade build
	$(VENV_PYTHON) -m build
	@echo "$(GREEN)Build complete. Check dist/ directory$(NC)"

.PHONY: publish-test
publish-test: build ## Publish to TestPyPI
	@echo "$(BLUE)Publishing to TestPyPI...$(NC)"
	$(VENV_PYTHON) -m pip install --upgrade twine
	$(VENV_PYTHON) -m twine upload --repository testpypi dist/*
	@echo "$(GREEN)Published to TestPyPI$(NC)"

.PHONY: publish
publish: build ## Publish to PyPI
	@echo "$(RED)Publishing to PyPI...$(NC)"
	@echo "$(RED)Are you sure? This will publish to the real PyPI! [y/N]$(NC)"
	@read -r response; \
	if [ "$$response" = "y" ] || [ "$$response" = "Y" ]; then \
		$(VENV_PYTHON) -m pip install --upgrade twine; \
		$(VENV_PYTHON) -m twine upload dist/*; \
		echo "$(GREEN)Published to PyPI$(NC)"; \
	else \
		echo "$(BLUE)Publish cancelled$(NC)"; \
	fi

.PHONY: dev
dev: install-dev ## Set up complete development environment
	@echo "$(GREEN)Development environment ready!$(NC)"
	@echo "$(BLUE)Activate with: source $(VENV)/bin/activate$(NC)"

.PHONY: run-example
run-example: install ## Run example script
	@echo "$(BLUE)Running example...$(NC)"
	$(VENV_PYTHON) examples/basic_example.py

.PHONY: benchmark
benchmark: install ## Run performance benchmarks
	@echo "$(BLUE)Running benchmarks...$(NC)"
	$(VENV_PYTHON) benchmarks/benchmark_queue.py
	$(VENV_PYTHON) benchmarks/benchmark_stack.py

.PHONY: docs
docs: ## Generate documentation
	@echo "$(BLUE)Generating documentation...$(NC)"
	$(VENV_PIP) install sphinx sphinx-rtd-theme
	cd docs && $(VENV)/bin/sphinx-build -b html . _build/html
	@echo "$(GREEN)Documentation built in docs/_build/html$(NC)"

.PHONY: serve-docs
serve-docs: docs ## Serve documentation locally
	@echo "$(BLUE)Serving documentation at http://localhost:8000$(NC)"
	cd docs/_build/html && python3 -m http.server

.PHONY: version
version: ## Show package version
	@$(VENV_PYTHON) -c "import zeroipc; print(f'ZeroIPC version: {zeroipc.__version__}')"

.PHONY: shell
shell: venv ## Open Python shell with package imported
	@$(VENV_PYTHON) -c "import zeroipc; import IPython; IPython.embed()" || \
		$(VENV_PYTHON) -i -c "import zeroipc; print('ZeroIPC imported')"

# Default target
.DEFAULT_GOAL := help