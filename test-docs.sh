#!/bin/bash
# Test MkDocs documentation setup

set -e

echo "Testing MkDocs Documentation Setup"
echo "=================================="
echo

# Check if Python is available
if ! command -v python3 &> /dev/null; then
    echo "ERROR: python3 not found"
    exit 1
fi
echo "✓ Python3 found"

# Check if pip is available
if ! command -v pip3 &> /dev/null; then
    echo "ERROR: pip3 not found"
    exit 1
fi
echo "✓ pip3 found"

# Check for virtual environment (optional but recommended)
if [ -n "$VIRTUAL_ENV" ]; then
    echo "✓ Running in virtual environment: $VIRTUAL_ENV"
else
    echo "⚠ Not in virtual environment (recommended but optional)"
fi

# Install dependencies
echo
echo "Installing MkDocs dependencies..."
pip3 install -q -r requirements-docs.txt
echo "✓ Dependencies installed"

# Check if mkdocs is available
if ! command -v mkdocs &> /dev/null; then
    echo "ERROR: mkdocs not found after installation"
    exit 1
fi
echo "✓ mkdocs command available"

# Validate mkdocs.yml
echo
echo "Validating mkdocs.yml..."
if mkdocs build --strict 2>&1 | grep -q "ERROR"; then
    echo "ERROR: mkdocs build failed"
    mkdocs build --strict
    exit 1
fi
echo "✓ mkdocs.yml is valid"

# Check for required files
echo
echo "Checking required files..."
required_files=(
    "mkdocs.yml"
    "docs/index.md"
    "docs/getting-started/index.md"
    "docs/getting-started/installation.md"
    "docs/getting-started/quick-start.md"
    "docs/getting-started/concepts.md"
    "docs/cli/index.md"
    "docs/tutorial/index.md"
    "docs/api/index.md"
    "docs/stylesheets/extra.css"
)

missing_files=0
for file in "${required_files[@]}"; do
    if [ -f "$file" ]; then
        echo "  ✓ $file"
    else
        echo "  ✗ $file (MISSING)"
        missing_files=$((missing_files + 1))
    fi
done

if [ $missing_files -gt 0 ]; then
    echo
    echo "ERROR: $missing_files required file(s) missing"
    exit 1
fi

# Build documentation
echo
echo "Building documentation..."
if mkdocs build --strict; then
    echo "✓ Documentation built successfully"
    echo "  Output in: site/"
else
    echo "ERROR: Documentation build failed"
    exit 1
fi

# Check output
if [ -f "site/index.html" ]; then
    echo "✓ Generated site/index.html"
else
    echo "ERROR: site/index.html not generated"
    exit 1
fi

# Summary
echo
echo "=================================="
echo "All tests passed! ✓"
echo
echo "Documentation is ready to use."
echo
echo "To view locally:"
echo "  mkdocs serve"
echo
echo "To deploy to GitHub Pages:"
echo "  mkdocs gh-deploy"
echo
echo "Documentation structure:"
echo "  - Getting Started: Complete (4 pages)"
echo "  - CLI Tool Guide: Complete (5 pages)"
echo "  - Tutorial: Index ready (lessons to be completed)"
echo "  - API Reference: Index ready (details to be completed)"
echo "  - Examples: Index ready (examples to be completed)"
echo "  - Best Practices: Index ready (details to be completed)"
echo "  - Advanced Topics: Index ready (details to be completed)"
echo

exit 0
