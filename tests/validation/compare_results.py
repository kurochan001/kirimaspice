#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
compare_results.py - Compare ngspice and KirimaSpice simulation results
"""

import os
import sys
import re
import argparse
from pathlib import Path
from typing import List, Tuple, Dict
import subprocess
import tempfile


class SimulationResult:
    """Container for simulation results"""
    def __init__(self, name: str):
        self.name = name
        self.time = []
        self.voltages = {}  # node_name -> [values]
        self.currents = {}  # source_name -> [values]

    def add_timepoint(self, t: float):
        self.time.append(t)

    def add_voltage(self, node: str, v: float):
        if node not in self.voltages:
            self.voltages[node] = []
        self.voltages[node].append(v)

    def add_current(self, source: str, i: float):
        if source not in self.currents:
            self.currents[source] = []
        self.currents[source].append(i)


def parse_ngspice_output(filepath: Path) -> SimulationResult:
    """Parse ngspice ASCII output file"""
    result = SimulationResult("ngspice")

    if not filepath.exists():
        raise FileNotFoundError(f"ngspice output file not found: {filepath}")

    with open(filepath, 'r') as f:
        lines = f.readlines()

    # Find the data section
    data_start = -1
    header_line = -1

    for i, line in enumerate(lines):
        if 'Index' in line and 'time' in line:
            header_line = i
            data_start = i + 1
            break

    if data_start == -1:
        raise ValueError(f"Could not find data section in {filepath}")

    # Parse header to get column names
    header = lines[header_line].strip()
    # Example header: "Index   time            v(n1)           v(n2)"
    columns = header.split()

    # Parse data lines
    for line in lines[data_start:]:
        line = line.strip()
        if not line or line.startswith('---'):
            continue

        # Split by whitespace
        values = line.split()
        if len(values) < 2:
            continue

        try:
            # Index is first, time is second
            time_val = float(values[1])
            result.add_timepoint(time_val)

            # Parse remaining columns
            for col_idx in range(2, min(len(columns), len(values))):
                col_name = columns[col_idx].lower()
                value = float(values[col_idx])

                # Check if it's a voltage or current
                if col_name.startswith('v('):
                    # Extract node name: v(n1) -> n1
                    node = col_name[2:-1]
                    result.add_voltage(node, value)
                elif col_name.startswith('i('):
                    # Extract source name: i(v1) -> v1
                    source = col_name[2:-1]
                    result.add_current(source, value)

        except (ValueError, IndexError) as e:
            print(f"Warning: Skipping malformed line: {line}")
            continue

    return result


def run_kirimaspice(netlist_path: Path) -> SimulationResult:
    """Run KirimaSpice simulation and return results"""
    result = SimulationResult("KirimaSpice")

    # Convert netlist path to KirimaSpice version (with _kirimaspice suffix)
    kirimaspice_netlist = netlist_path.parent / f"{netlist_path.stem}_kirimaspice.cir"

    if not kirimaspice_netlist.exists():
        print(f"Warning: KirimaSpice netlist not found: {kirimaspice_netlist}")
        return result

    # Find kirimaspice binary
    kirimaspice_binary = Path("build/kirimaspice")
    if not kirimaspice_binary.exists():
        kirimaspice_binary = Path("./kirimaspice")
        if not kirimaspice_binary.exists():
            print(f"Error: kirimaspice binary not found")
            return result

    try:
        # Run KirimaSpice
        result_subprocess = subprocess.run(
            [str(kirimaspice_binary), str(kirimaspice_netlist)],
            capture_output=True,
            text=True,
            timeout=30
        )

        if result_subprocess.returncode != 0:
            print(f"Error: KirimaSpice exited with code {result_subprocess.returncode}")
            print(f"stderr: {result_subprocess.stderr}")
            return result

        # Parse output
        lines = result_subprocess.stdout.split('\n')

        # Find the data section
        data_start = -1
        for i, line in enumerate(lines):
            if 'Time (s)' in line:
                data_start = i + 1
                break

        if data_start == -1:
            print("Error: Could not find data section in KirimaSpice output")
            return result

        # Parse node names from header
        header_line = lines[data_start - 1]
        # Example: "Time (s)\tV(0)\tV(n1)\tV(n2)\t"
        node_names = []
        for col in header_line.split('\t'):
            col = col.strip()
            if col.startswith('V(') and col.endswith(')'):
                node_name = col[2:-1]  # Extract node name from V(...)
                node_names.append(node_name)

        # Parse data lines
        for line in lines[data_start:]:
            line = line.strip()
            if not line:
                continue

            values = line.split('\t')
            if len(values) < 2:
                continue

            try:
                time_val = float(values[0])
                result.add_timepoint(time_val)

                # Parse node voltages
                for idx, node_name in enumerate(node_names):
                    if idx + 1 < len(values):
                        voltage = float(values[idx + 1])
                        result.add_voltage(node_name, voltage)
            except (ValueError, IndexError):
                continue

        return result

    except subprocess.TimeoutExpired:
        print("Error: KirimaSpice execution timed out")
        return result
    except Exception as e:
        print(f"Error running KirimaSpice: {e}")
        return result


def linear_interp(x: float, x_arr: List[float], y_arr: List[float]) -> float:
    """Linear interpolation at point x"""
    if x <= x_arr[0]:
        return y_arr[0]
    if x >= x_arr[-1]:
        return y_arr[-1]

    # Find bracketing points
    for i in range(len(x_arr) - 1):
        if x_arr[i] <= x <= x_arr[i+1]:
            # Linear interpolation
            t = (x - x_arr[i]) / (x_arr[i+1] - x_arr[i])
            return y_arr[i] + t * (y_arr[i+1] - y_arr[i])

    return y_arr[-1]


def calculate_error_metrics(ref: SimulationResult, test: SimulationResult,
                            signal_name: str) -> Dict[str, float]:
    """Calculate error metrics between reference and test signals with interpolation"""

    # Find common signal
    ref_signal = None
    test_signal = None

    # Check voltages
    if signal_name in ref.voltages and signal_name in test.voltages:
        ref_signal = ref.voltages[signal_name]
        test_signal = test.voltages[signal_name]
    # Check currents
    elif signal_name in ref.currents and signal_name in test.currents:
        ref_signal = ref.currents[signal_name]
        test_signal = test.currents[signal_name]

    if ref_signal is None or test_signal is None:
        raise ValueError(f"Signal '{signal_name}' not found in both results")

    # Get time arrays
    ref_time = ref.time
    ref_values = ref_signal
    test_time = test.time
    test_values = test_signal

    # Only compare timepoints within the common time range
    min_time = max(ref_time[0], test_time[0])
    max_time = min(ref_time[-1], test_time[-1])

    # Filter test timepoints to common range and interpolate
    abs_errors = []
    rel_errors = []

    for i, t in enumerate(test_time):
        if min_time <= t <= max_time:
            # Interpolate reference value at this test timepoint
            ref_val = linear_interp(t, ref_time, ref_values)
            test_val = test_values[i]

            # Calculate errors
            abs_err = abs(ref_val - test_val)
            abs_errors.append(abs_err)

            if abs(ref_val) > 1e-12:
                rel_err = abs((ref_val - test_val) / ref_val)
            else:
                rel_err = 0.0
            rel_errors.append(rel_err)

    if len(abs_errors) == 0:
        raise ValueError(f"No overlapping timepoints for signal '{signal_name}'")

    metrics = {
        'max_abs_error': max(abs_errors),
        'mean_abs_error': sum(abs_errors) / len(abs_errors),
        'max_rel_error': max(rel_errors),
        'mean_rel_error': sum(rel_errors) / len(rel_errors),
        'num_points': len(abs_errors),
    }

    return metrics


def print_report(circuit_name: str, ngspice_result: SimulationResult,
                 kirimaspice_result: SimulationResult):
    """Print comparison report"""

    print(f"\n{'='*70}")
    print(f"Comparison Report: {circuit_name}")
    print(f"{'='*70}\n")

    print(f"ngspice results:")
    print(f"  Timepoints: {len(ngspice_result.time)}")
    print(f"  Voltages: {list(ngspice_result.voltages.keys())}")
    print(f"  Currents: {list(ngspice_result.currents.keys())}")

    if ngspice_result.time:
        print(f"  Time range: [{ngspice_result.time[0]:.6f}, {ngspice_result.time[-1]:.6f}]")

    print(f"\nKirimaSpice results:")
    print(f"  Timepoints: {len(kirimaspice_result.time)}")
    print(f"  Voltages: {list(kirimaspice_result.voltages.keys())}")
    print(f"  Currents: {list(kirimaspice_result.currents.keys())}")

    if kirimaspice_result.time:
        print(f"  Time range: [{kirimaspice_result.time[0]:.6f}, {kirimaspice_result.time[-1]:.6f}]")

    # Calculate error metrics if both results are available
    if len(ngspice_result.time) > 0 and len(kirimaspice_result.time) > 0:
        print(f"\n{'='*70}")
        print("Error Metrics:")
        print(f"{'='*70}\n")

        # Find common voltage signals
        common_voltages = set(ngspice_result.voltages.keys()) & set(kirimaspice_result.voltages.keys())

        if common_voltages:
            for signal in sorted(common_voltages):
                # Skip ground node (always 0)
                if signal == '0':
                    continue

                try:
                    metrics = calculate_error_metrics(ngspice_result, kirimaspice_result, signal)

                    print(f"Signal: V({signal})")
                    print(f"  Max absolute error: {metrics['max_abs_error']:.6e} V")
                    print(f"  Mean absolute error: {metrics['mean_abs_error']:.6e} V")
                    print(f"  Max relative error: {metrics['max_rel_error']:.2%}")
                    print(f"  Mean relative error: {metrics['mean_rel_error']:.2%}")
                    print(f"  Compared points: {metrics['num_points']}")
                    print()

                except Exception as e:
                    print(f"  Error calculating metrics for V({signal}): {e}\n")
        else:
            print("No common voltage signals found for comparison\n")

    print(f"{'='*70}\n")


def main():
    parser = argparse.ArgumentParser(
        description='Compare ngspice and KirimaSpice simulation results')
    parser.add_argument('circuit', type=str,
                       help='Circuit name (without .cir extension)')
    parser.add_argument('--results-dir', type=str,
                       default='tests/validation/results',
                       help='Results directory')
    parser.add_argument('--circuits-dir', type=str,
                       default='tests/validation/circuits',
                       help='Circuits directory')

    args = parser.parse_args()

    # Construct paths
    script_dir = Path(__file__).parent
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

    # Parse ngspice results
    print(f"Parsing ngspice results: {ngspice_output}")
    ngspice_result = parse_ngspice_output(ngspice_output)

    # Run KirimaSpice
    print(f"Running KirimaSpice simulation: {netlist_path}")
    kirimaspice_result = run_kirimaspice(netlist_path)

    # Print report
    print_report(args.circuit, ngspice_result, kirimaspice_result)


if __name__ == '__main__':
    main()
