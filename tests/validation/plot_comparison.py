#!/usr/bin/env python3
"""
plot_comparison.py - Visualize ngspice vs KirimaSpice comparison
"""

import os
import sys
import argparse
from pathlib import Path
from typing import List

# Add parent directory to path to import compare_results
sys.path.insert(0, str(Path(__file__).parent))
from compare_results import (
    parse_ngspice_output,
    run_kirimaspice,
    SimulationResult
)

try:
    import matplotlib.pyplot as plt
    import matplotlib.gridspec as gridspec
    MATPLOTLIB_AVAILABLE = True
except ImportError:
    MATPLOTLIB_AVAILABLE = False
    print("Warning: matplotlib not available. Please install with:")
    print("  pip3 install --user matplotlib")
    print("or")
    print("  pip3 install --break-system-packages matplotlib")


def plot_waveforms(ngspice_result: SimulationResult,
                   kirimaspice_result: SimulationResult,
                   circuit_name: str,
                   output_dir: Path):
    """Plot voltage waveforms comparison"""

    if not MATPLOTLIB_AVAILABLE:
        print("Cannot create plots without matplotlib")
        return

    # Find common voltage signals
    common_signals = set(ngspice_result.voltages.keys()) & set(kirimaspice_result.voltages.keys())

    if not common_signals:
        print("No common signals to plot")
        return

    # Sort signals (ground first, then others)
    signals = sorted(common_signals, key=lambda x: (x != '0', x))

    # Skip ground node (always 0)
    signals = [s for s in signals if s != '0']

    if not signals:
        print("No non-zero signals to plot")
        return

    # Create figure with subplots
    n_signals = len(signals)
    fig = plt.figure(figsize=(12, 3 * n_signals))
    gs = gridspec.GridSpec(n_signals, 1, hspace=0.3)

    for idx, signal in enumerate(signals):
        ax = fig.add_subplot(gs[idx])

        # Plot ngspice reference
        ax.plot(ngspice_result.time, ngspice_result.voltages[signal],
                'b-', linewidth=2, label='ngspice (reference)', alpha=0.7)

        # Plot KirimaSpice
        ax.plot(kirimaspice_result.time, kirimaspice_result.voltages[signal],
                'r--', linewidth=2, label='KirimaSpice', alpha=0.7)

        ax.set_xlabel('Time (s)')
        ax.set_ylabel(f'V({signal}) (V)')
        ax.set_title(f'{circuit_name}: V({signal})')
        ax.legend(loc='best')
        ax.grid(True, alpha=0.3)

    # Save figure
    output_path = output_dir / f'{circuit_name}_waveforms.png'
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Saved waveform plot: {output_path}")
    plt.close()


def plot_error(ngspice_result: SimulationResult,
               kirimaspice_result: SimulationResult,
               circuit_name: str,
               output_dir: Path):
    """Plot absolute and relative errors"""

    if not MATPLOTLIB_AVAILABLE:
        return

    # Find common voltage signals
    common_signals = set(ngspice_result.voltages.keys()) & set(kirimaspice_result.voltages.keys())
    signals = [s for s in sorted(common_signals) if s != '0']

    if not signals:
        return

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))

    for signal in signals:
        # Get data
        ng_time = ngspice_result.time
        ng_voltage = ngspice_result.voltages[signal]
        sk_time = kirimaspice_result.time
        sk_voltage = kirimaspice_result.voltages[signal]

        # Interpolate ngspice at kirimaspice timepoints
        import numpy as np
        ng_interp = np.interp(sk_time, ng_time, ng_voltage)

        # Calculate errors
        abs_error = np.abs(ng_interp - sk_voltage)
        rel_error = np.where(np.abs(ng_interp) > 1e-12,
                            np.abs((ng_interp - sk_voltage) / ng_interp) * 100,
                            0.0)

        # Plot absolute error
        ax1.plot(sk_time, abs_error, label=f'V({signal})', linewidth=2)

        # Plot relative error
        ax2.plot(sk_time, rel_error, label=f'V({signal})', linewidth=2)

    ax1.set_xlabel('Time (s)')
    ax1.set_ylabel('Absolute Error (V)')
    ax1.set_title(f'{circuit_name}: Absolute Error')
    ax1.legend(loc='best')
    ax1.grid(True, alpha=0.3)

    ax2.set_xlabel('Time (s)')
    ax2.set_ylabel('Relative Error (%)')
    ax2.set_title(f'{circuit_name}: Relative Error')
    ax2.legend(loc='best')
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()

    # Save figure
    output_path = output_dir / f'{circuit_name}_errors.png'
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    print(f"Saved error plot: {output_path}")
    plt.close()


def main():
    parser = argparse.ArgumentParser(
        description='Plot ngspice vs KirimaSpice comparison')
    parser.add_argument('circuit', type=str,
                       help='Circuit name (without .cir extension)')
    parser.add_argument('--results-dir', type=str,
                       default='tests/validation/results',
                       help='Results directory')
    parser.add_argument('--circuits-dir', type=str,
                       default='tests/validation/circuits',
                       help='Circuits directory')
    parser.add_argument('--output-dir', type=str,
                       default='tests/validation/plots',
                       help='Output directory for plots')

    args = parser.parse_args()

    if not MATPLOTLIB_AVAILABLE:
        sys.exit(1)

    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Construct paths
    results_dir = Path(args.results_dir)
    circuits_dir = Path(args.circuits_dir)

    ngspice_output = results_dir / f"{args.circuit}_ngspice.txt"
    netlist_path = circuits_dir / f"{args.circuit}.cir"

    # Check if files exist
    if not ngspice_output.exists():
        print(f"Error: ngspice output not found: {ngspice_output}")
        print("Run ./run_ngspice.sh first to generate ngspice results")
        sys.exit(1)

    if not netlist_path.exists():
        print(f"Error: Circuit netlist not found: {netlist_path}")
        sys.exit(1)

    # Parse results
    print(f"Parsing ngspice results: {ngspice_output}")
    ngspice_result = parse_ngspice_output(ngspice_output)

    print(f"Running KirimaSpice simulation: {netlist_path}")
    kirimaspice_result = run_kirimaspice(netlist_path)

    # Generate plots
    print(f"\nGenerating plots...")
    plot_waveforms(ngspice_result, kirimaspice_result, args.circuit, output_dir)
    plot_error(ngspice_result, kirimaspice_result, args.circuit, output_dir)

    print(f"\nPlots saved to: {output_dir}")


if __name__ == '__main__':
    main()
