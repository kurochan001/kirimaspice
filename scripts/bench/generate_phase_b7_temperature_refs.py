#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Phase B.7: Generate ngspice reference data for BSIM4 temperature dependence validation

This script generates reference I-V curves at multiple temperatures (-40°C to 125°C)
for BSIM4 N-channel MOSFET to validate temperature-dependent parameters:
- KT1/KT2: Threshold voltage temperature coefficients
- UTE: Mobility temperature exponent
- AT: Saturation velocity temperature coefficient
"""

import subprocess
import json
import tempfile
import os
from pathlib import Path

# Temperature test points (Kelvin)
TEMPERATURES = [
    233.15,  # -40°C (automotive low limit)
    273.15,  #   0°C (freezing point)
    300.15,  #  27°C (nominal, SPICE standard)
    350.15,  #  77°C (typical high ambient)
    398.15,  # 125°C (automotive high limit)
]

# Bias conditions for temperature sweep
BIAS_POINTS = [
    # (VGS, VDS, VBS, description)
    (1.0, 0.1, 0.0, "Linear-LowVds"),      # Linear region, low drain voltage
    (1.5, 1.0, 0.0, "Saturation-Nominal"), # Saturation region, nominal bias
    (2.0, 2.0, 0.0, "Saturation-HighVgs"), # Saturation region, high gate voltage
]

# BSIM4 model parameters (from test_bsim4_comprehensive.cpp)
BSIM4_PARAMS = """
.model BSIM4N NMOS LEVEL=14
+ version=4.8.3
+ tnom=27
+ vth0=0.5 k1=0.5 k2=0.0 k3=80 k3b=0 w0=2.5e-6
+ dvt0=2.2 dvt1=0.53 dvt2=-0.032 dvt0w=0 dvt1w=5.3e6 dvt2w=-0.032
+ dsub=0.56 minv=0.05 voffl=0 dvtp0=0 dvtp1=0 lpe0=0 lpeb=0
+ xj=1.5e-7 ngate=2e20 ndep=1.7e17
+ nfactor=2 cdsc=0.00024 cdscb=0 cdscd=0 cit=0
+ voff=-0.08 ngate=2e20
+ u0=0.067 ua=1e-9 ub=1e-18 uc=-1e-10
+ kt1=-0.11 kt2=0.022 ute=-1.5 ua1=1e-9 ub1=-3.5e-18 uc1=0 at=3.3e4
+ vsat=8e4 a0=1.0 ags=0 a1=0 a2=1.0 b0=0 b1=0
+ keta=-0.047 dwg=0 dwb=0 pclm=1.3 pdiblc1=0.39 pdiblc2=0.0086 pdiblcb=0
+ eta0=0.08 etab=-0.07 drout=0.56
+ w=1e-6 l=50e-9
"""


def run_ngspice_simulation(vgs, vds, vbs, temperature_c):
    """
    Run ngspice DC simulation at specified bias and temperature

    Args:
        vgs: Gate-source voltage [V]
        vds: Drain-source voltage [V]
        vbs: Bulk-source voltage [V]
        temperature_c: Temperature [°C]

    Returns:
        dict: {vth, vdsat, ids, gm, gds} extracted from ngspice
    """
    # Create SPICE netlist
    netlist = f"""BSIM4 Temperature Sweep Test Circuit
{BSIM4_PARAMS}

* Test circuit
VGS gate 0 DC {vgs}
VDS drain 0 DC {vds}
VBS bulk 0 DC {vbs}
M1 drain gate 0 bulk BSIM4N W=1u L=50n

* Analysis
.option temp={temperature_c}
.control
op
print @m1[vth] @m1[vdsat] @m1[id] @m1[gm] @m1[gds]
quit
.endc
.end
"""

    # Write to temporary file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.sp', delete=False) as f:
        f.write(netlist)
        netlist_path = f.name

    try:
        # Run ngspice
        result = subprocess.run(
            ['ngspice', '-b', netlist_path],
            capture_output=True,
            text=True,
            timeout=10
        )

        # Parse output - ngspice outputs one value per line
        lines = result.stdout.split('\n')
        values = {}
        for line in lines:
            if '@m1[vth]' in line:
                parts = line.split('=')
                if len(parts) == 2:
                    values['vth'] = float(parts[1].strip())
            elif '@m1[vdsat]' in line:
                parts = line.split('=')
                if len(parts) == 2:
                    values['vdsat'] = float(parts[1].strip())
            elif '@m1[id]' in line:
                parts = line.split('=')
                if len(parts) == 2:
                    values['ids'] = float(parts[1].strip())
            elif '@m1[gm]' in line:
                parts = line.split('=')
                if len(parts) == 2:
                    values['gm'] = float(parts[1].strip())
            elif '@m1[gds]' in line:
                parts = line.split('=')
                if len(parts) == 2:
                    values['gds'] = float(parts[1].strip())

        if len(values) == 5:
            return values
        else:
            raise RuntimeError(f"Failed to parse all values from ngspice output. Got {len(values)}/5 values:\n{result.stdout}")

    finally:
        os.unlink(netlist_path)


def generate_temperature_sweep_refs():
    """Generate ngspice reference data for all temperature/bias combinations"""

    results = {}

    for temp_k in TEMPERATURES:
        temp_c = temp_k - 273.15
        print(f"\n=== Temperature: {temp_c:.1f}°C ({temp_k:.2f}K) ===")

        for vgs, vds, vbs, desc in BIAS_POINTS:
            key = f"{desc}_T{int(temp_c)}C"
            print(f"  Running {desc}: VGS={vgs}V, VDS={vds}V, VBS={vbs}V")

            try:
                result = run_ngspice_simulation(vgs, vds, vbs, temp_c)

                results[key] = {
                    'bias': {'vgs': vgs, 'vds': vds, 'vbs': vbs},
                    'temperature_k': temp_k,
                    'temperature_c': temp_c,
                    'ngspice': result
                }

                print(f"    Vth={result['vth']:.6f}V, Ids={result['ids']:.6e}A")

            except Exception as e:
                print(f"    ERROR: {e}")
                continue

    return results


def main():
    """Main entry point"""
    print("Phase B.7: Generating BSIM4 temperature dependence reference data")
    print("=" * 80)

    # Generate reference data
    results = generate_temperature_sweep_refs()

    # Save to JSON
    output_path = '/tmp/phase_b7_temperature_refs.json'
    with open(output_path, 'w') as f:
        json.dump(results, f, indent=2)

    print(f"\n{'=' * 80}")
    print(f"SUCCESS: Generated {len(results)} reference data points")
    print(f"Output: {output_path}")

    # Summary statistics
    print(f"\n=== Temperature Range ===")
    temps_c = sorted(set(r['temperature_c'] for r in results.values()))
    print(f"  {temps_c[0]:.1f}°C to {temps_c[-1]:.1f}°C ({len(temps_c)} points)")

    print(f"\n=== Bias Conditions ===")
    for vgs, vds, vbs, desc in BIAS_POINTS:
        print(f"  {desc}: VGS={vgs}V, VDS={vds}V, VBS={vbs}V")


if __name__ == '__main__':
    main()
