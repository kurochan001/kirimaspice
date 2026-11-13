# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import os
import subprocess
import numpy as np

def generate_netlist(filename, output_file):
    with open(filename, 'w') as f:
        f.write("* BSIM4 Inverter Chain Transient Test\n")
        f.write(".options method=gear\n")  # Gear2 integration
        
        # BSIM4 Models (Minimal parameters matching kirimaspice tests)
        f.write(".model n1 nmos level=14 version=4.8.1 \n")
        f.write("+ toxe=3.0e-9 vth0=0.5 u0=0.067 vsat=8.0e4 \n")
        f.write("+ w=10e-6 l=180e-9 \n")
        f.write("+ cgso=2.0e-10 cgdo=2.0e-10 cgbo=1.0e-10 \n") # Overlap caps
        
        f.write(".model p1 pmos level=14 version=4.8.1 \n")
        f.write("+ toxe=3.0e-9 vth0=-0.5 u0=0.025 vsat=8.0e4 \n")
        f.write("+ w=20e-6 l=180e-9 \n")
        f.write("+ cgso=2.0e-10 cgdo=2.0e-10 cgbo=1.0e-10 \n")

        # Supply
        f.write("Vdd vdd 0 1.8\n")
        
        # Input Pulse
        f.write("Vin vin 0 PULSE(0 1.8 1ns 100ps 100ps 4ns 10ns)\n")
        
        # Inverter Chain (3 stages)
        # Stage 1
        f.write("Mp1 n1 vin vdd vdd p1\n")
        f.write("Mn1 n1 vin 0 0 n1\n")
        f.write("Cload1 n1 0 10fF\n")
        
        # Stage 2
        f.write("Mp2 n2 n1 vdd vdd p1\n")
        f.write("Mn2 n2 n1 0 0 n1\n")
        f.write("Cload2 n2 0 10fF\n")
        
        # Stage 3
        f.write("Mp3 n3 n2 vdd vdd p1\n")
        f.write("Mn3 n3 n2 0 0 n1\n")
        f.write("Cload3 n3 0 10fF\n")

        # Transient Analysis
        f.write(".tran 10ps 10ns\n")
        
        # Output control
        f.write(".control\n")
        f.write("run\n")
        # Write data to file directly using wrdata
        # Format: time v(vin) v(n1) v(n2) v(n3)
        f.write("wrdata {} v(vin) v(n1) v(n2) v(n3)\n".format(output_file))
        f.write(".endc\n")
        f.write(".end\n")

def run_ngspice(netlist, output_file):
    # We don't need to capture stdout for data now, but we run it
    # output_file is passed to the netlist for wrdata
    cmd = f"ngspice -b {netlist} > /dev/null"
    subprocess.run(cmd, shell=True, check=True)

def parse_output(output_file, ref_file):
    data = []
    # wrdata output format: time val1 time val2 time val3 ...
    # Wait, ngspice wrdata usually outputs: time value1 value2 ...
    # Let's check the format. Usually it's: time v(vin) v(n1) v(n2) v(n3)
    # BUT sometimes it repeats time column for each variable if not configured otherwise.
    # Let's assume standard behavior for now or check.
    # Actually, wrdata outputs: col1 col2 col3 ...
    # If we ask for multiple vectors, it might be: time1 v1 time2 v2 ...
    # To be safe, let's read the file and inspect.
    
    # Better approach: simple parsing of space-separated values
    with open(output_file, 'r') as f:
        lines = f.readlines()

    with open(ref_file, 'w') as f:
        f.write("# Time Vin V_n1 V_n2 V_n3\n")
        for line in lines:
            if not line.strip(): continue
            parts = line.split()
            if len(parts) >= 8: # time v1 time v2 time v3 time v4
                # wrdata format is often: time1 val1 time2 val2 ...
                # So for 4 signals: t v1 t v2 t v3 t v4
                try:
                    t = float(parts[0])
                    v1 = float(parts[1])
                    v2 = float(parts[3])
                    v3 = float(parts[5])
                    v4 = float(parts[7])
                    f.write(f"{t:.6e} {v1:.6e} {v2:.6e} {v3:.6e} {v4:.6e}\n")
                except ValueError:
                    continue
    
    print(f"Generated reference points in {ref_file}")

if __name__ == "__main__":
    netlist_path = "/tmp/bsim4_inv_chain.sp"
    output_path = "/tmp/bsim4_inv_chain.out"
    ref_path = "validation/bsim4_inv_chain_tran_ref.txt"
    
    # Ensure validation dir exists
    os.makedirs("validation", exist_ok=True)
    
    generate_netlist(netlist_path, output_path)
    run_ngspice(netlist_path, output_path)
    parse_output(output_path, ref_path)
