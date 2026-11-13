#!/bin/bash
# run_ngspice.sh - Run ngspice simulations for validation tests

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CIRCUITS_DIR="$SCRIPT_DIR/circuits"
RESULTS_DIR="$SCRIPT_DIR/results"

# Check if ngspice is installed
if ! command -v ngspice &> /dev/null; then
    echo "Error: ngspice is not installed"
    echo "Please install ngspice:"
    echo "  Ubuntu/Debian: sudo apt install ngspice"
    echo "  macOS: brew install ngspice"
    exit 1
fi

# Create results directory if it doesn't exist
mkdir -p "$RESULTS_DIR"

echo "=== Running ngspice validation tests ==="
echo "Circuits directory: $CIRCUITS_DIR"
echo "Results directory: $RESULTS_DIR"
echo ""

# Run each circuit
for circuit in "$CIRCUITS_DIR"/*.cir; do
    circuit_name=$(basename "$circuit" .cir)
    echo "Running: $circuit_name"

    # Change to validation directory so ngspice can find results/ path
    cd "$SCRIPT_DIR"

    # Run ngspice in batch mode
    ngspice -b "$circuit" -o "${RESULTS_DIR}/${circuit_name}_log.txt"

    if [ $? -eq 0 ]; then
        echo "  ✓ Success"
    else
        echo "  ✗ Failed"
        exit 1
    fi
done

echo ""
echo "=== All ngspice simulations completed ==="
echo "Results saved in: $RESULTS_DIR"
