#!/usr/bin/env python3
# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""
Generate ngspice reference data for the BSIM4 multi-bias point validation
in tests/unit/test_bsim4_comprehensive.cpp.
This script creates SPICE decks for 13 bias points and runs ngspice to extract
operating point data (ids, vth, vdsat).
"""

import subprocess
import re
import os
import json

# BSIM4 model parameters matching QA deck
MODEL_PARAMS = {
    'vth0': 0.45,
    'k1': 0.53,
    'k2': 0.02,
    'u0': 0.045,
    'vsat': 8.5e4,
    'pclm': 1.3,
    'toxe': 1.25e-9,
    'w': 10e-6,
    'l': 45e-9,
}

# Test matrix: (VGS, VDS, VBS, region_name)
TEST_POINTS = [
    # Subthreshold region (VGS < Vth)
    (0.2, 0.5, 0.0, "Subthreshold-Low"),
    (0.2, 1.0, 0.0, "Subthreshold-High"),
    (0.3, 1.0, 0.0, "Near-Threshold"),

    # Linear region (VGS > Vth, VDS < VGS-Vth)
    (1.0, 0.2, 0.0, "Linear-LowVds"),
    (1.5, 0.5, 0.0, "Linear-MedVds"),
    (2.0, 0.8, 0.0, "Linear-HighVgs"),

    # Saturation region (VGS > Vth, VDS > VGS-Vth)
    (1.0, 1.0, 0.0, "Saturation-LowVgs"),
    (1.5, 2.0, 0.0, "Saturation-MedVgs"),
    (2.0, 3.0, 0.0, "Saturation-HighVgs"),
    (2.5, 5.0, 0.0, "Saturation-VeryHighVds"),

    # Body effect tests (VBS < 0)
    (1.5, 1.0, -0.5, "Saturation-BodyBias0.5"),
    (1.5, 1.0, -1.0, "Saturation-BodyBias1.0"),
    (2.0, 2.0, -0.5, "Saturation-HighVgs-BodyBias"),
]

def create_spice_deck(vgs, vds, vbs, output_file):
    """Create SPICE deck for a single bias point."""
    deck = f"""* BSIM4 Bias Point Reference
VDS drain 0 DC {vds}
VGS gate 0 DC {vgs}
VBS bulk 0 DC {vbs}

.model QA_NMOS nmos level=14 version=4.8.3 +
+ vth0={MODEL_PARAMS['vth0']} +
+ k1={MODEL_PARAMS['k1']} +
+ k2={MODEL_PARAMS['k2']} +
+ u0={MODEL_PARAMS['u0']} +
+ vsat={MODEL_PARAMS['vsat']} +
+ pclm={MODEL_PARAMS['pclm']} +
+ toxe={MODEL_PARAMS['toxe']} +
+ w={MODEL_PARAMS['w']} +
+ l={MODEL_PARAMS['l']}

M1 drain gate 0 bulk QA_NMOS

.control
op
print @m1[vdsat]
print @m1[vth]
print @m1[id]
print @m1[gm]
print @m1[gds]
quit
.endc
.end
"""
    with open(output_file, 'w') as f:
        f.write(deck)

def run_ngspice(spice_file):
    """Run ngspice and extract operating point data."""
    try:
        result = subprocess.run(
            ['ngspice', '-b', spice_file],
            capture_output=True,
            text=True,
            timeout=10
        )

        output = result.stdout

        # Extract values using regex
        vdsat_match = re.search(r'@m1\[vdsat\]\s*=\s*([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)', output)
        vth_match = re.search(r'@m1\[vth\]\s*=\s*([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)', output)
        id_match = re.search(r'@m1\[id\]\s*=\s*([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)', output)
        gm_match = re.search(r'@m1\[gm\]\s*=\s*([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)', output)
        gds_match = re.search(r'@m1\[gds\]\s*=\s*([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)', output)

        if not all([vdsat_match, vth_match, id_match]):
            print(f"Warning: Could not extract all values from ngspice output")
            print(output)
            return None

        return {
            'vdsat': float(vdsat_match.group(1)),
            'vth': float(vth_match.group(1)),
            'ids': float(id_match.group(1)),
            'gm': float(gm_match.group(1)) if gm_match else 0.0,
            'gds': float(gds_match.group(1)) if gds_match else 0.0,
        }
    except Exception as e:
        print(f"Error running ngspice: {e}")
        return None

def main():
    """Generate reference data for all test points."""
    results = {}

    for vgs, vds, vbs, region in TEST_POINTS:
        print(f"Generating reference for {region} (VGS={vgs}V, VDS={vds}V, VBS={vbs}V)...")

        # Create SPICE deck
        spice_file = f"/tmp/bsim4_bias_{region.replace('-', '_').lower()}.sp"
        create_spice_deck(vgs, vds, vbs, spice_file)

        # Run ngspice
        op_data = run_ngspice(spice_file)

        if op_data:
            results[region] = {
                'bias': {'vgs': vgs, 'vds': vds, 'vbs': vbs},
                'ngspice': op_data
            }
            print(f"  → Ids={op_data['ids']:.6e}A, Vth={op_data['vth']:.4f}V, Vdsat={op_data['vdsat']:.4f}V")
        else:
            print(f"  → Failed to extract data")

        # Clean up
        if os.path.exists(spice_file):
            os.remove(spice_file)

    # Save results to JSON
    output_json = "/tmp/bsim4_bias_refs.json"
    with open(output_json, 'w') as f:
        json.dump(results, f, indent=2)

    print(f"\n✅ Reference data saved to {output_json}")
    print(f"Generated {len(results)}/{len(TEST_POINTS)} reference points")

if __name__ == '__main__':
    main()
