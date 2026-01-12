#!/usr/bin/env python3
import os
import sys
import glob
import re

def parse_ffmpeg_log(log_path):
    """Parses ffmpeg decoder log to extract total frames and average FPS."""
    if not os.path.exists(log_path):
        return 0, 0.0
    
    frames = 0
    fps = 0.0
    
    with open(log_path, 'r') as f:
        # Read the file from the end to find the summary line "frame=..."
        try:
            # Quick hack: read all lines and check the last few
            lines = f.readlines()
            for line in reversed(lines[-10:]):
                # Search for pattern: frame=  382 fps=141 q=-0.0 ...
                match = re.search(r'frame=\s*(\d+)\s+fps=\s*([\d\.]+)', line)
                if match:
                    frames = int(match.group(1))
                    fps = float(match.group(2))
                    return frames, fps
        except Exception:
            pass
            
    return frames, fps

def analyze_batch(batch_dir):
    print(f"Analyzing results in: {batch_dir}")
    print("-" * 80)
    print(f"{'Scenario':<25} | {'UDP Frames':<10} | {'XQUIC Frames':<12} | {'Improvement':<12}")
    print("-" * 80)
    
    scenarios = [d for d in os.listdir(batch_dir) if os.path.isdir(os.path.join(batch_dir, d))]
    
    # Sort scenarios to match batch order loosely
    order = ["baseline", "packet_loss_2pct", "packet_loss_5pct", "packet_loss_10pct", "high_latency_50ms", "limited_bandwidth", "challenging"]
    scenarios.sort(key=lambda x: order.index(x) if x in order else 99)

    for scenario in scenarios:
        scenario_path = os.path.join(batch_dir, scenario)
        udp_log = os.path.join(scenario_path, "logs", "decoder_left.log")
        xquic_log = os.path.join(scenario_path, "logs", "decoder_right.log")
        
        udp_frames, _ = parse_ffmpeg_log(udp_log)
        xquic_frames, _ = parse_ffmpeg_log(xquic_log)
        
        diff = xquic_frames - udp_frames
        pct = 0
        if udp_frames > 0:
            pct = (diff / udp_frames) * 100
            
        print(f"{scenario:<25} | {udp_frames:<10} | {xquic_frames:<12} | {diff:+d} ({pct:+.1f}%)")

    print("-" * 80)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: ./analyze_results.py <batch_results_dir>")
        sys.exit(1)
        
    analyze_batch(sys.argv[1])
