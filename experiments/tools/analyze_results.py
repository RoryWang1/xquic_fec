#!/usr/bin/env python3
"""
analyze_results.py - 分析XQUIC实验结果

从实验日志中提取关键指标并生成对比报告
"""

import json
import os
import sys
import re
from pathlib import Path
from typing import Dict, List, Any
import argparse


class ExperimentAnalyzer:
    """实验结果分析器"""
    
    def __init__(self, results_dir: str):
        self.results_dir = Path(results_dir)
        self.scenarios = []
        self.metrics = {}
        
    def scan_scenarios(self):
        """扫描所有场景结果"""
        if not self.results_dir.exists():
            print(f"Error: Results directory not found: {self.results_dir}")
            return False
            
        # 检查是否是batch结果目录
        if (self.results_dir / "batch_summary.txt").exists():
            # Batch结果，扫描子目录
            for item in self.results_dir.iterdir():
                if item.is_dir() or item.is_symlink():
                    scenario_name = item.name
                    if (item / "metrics.json").exists():
                        self.scenarios.append((scenario_name, item))
        else:
            # 单个场景或场景目录
            if (self.results_dir / "metrics.json").exists():
                # 单个实验
                scenario_name = self.results_dir.parent.name
                self.scenarios.append((scenario_name, self.results_dir))
            else:
                # 扫描子目录
                for scenario_dir in self.results_dir.iterdir():
                    if scenario_dir.is_dir():
                        # 找最新的时间戳目录
                        timestamp_dirs = sorted(scenario_dir.glob("*"), reverse=True)
                        if timestamp_dirs and (timestamp_dirs[0] / "metrics.json").exists():
                            self.scenarios.append((scenario_dir.name, timestamp_dirs[0]))
        
        print(f"Found {len(self.scenarios)} scenario(s) to analyze")
        return len(self.scenarios) > 0
    
    def extract_ffmpeg_metrics(self, log_file: Path) -> Dict[str, Any]:
        """从FFmpeg日志中提取指标"""
        metrics = {
            "fps": 0.0,
            "bitrate_kbps": 0.0,
            "frames_dropped": 0,
            "errors": []
        }
        
        if not log_file.exists():
            return metrics
        
        try:
            with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                
                # 提取fps (frame= 1234 fps=30 ...)
                fps_matches = re.findall(r'fps=\s*(\d+\.?\d*)', content)
                if fps_matches:
                    fps_values = [float(x) for x in fps_matches if x]
                    if fps_values:
                        metrics["fps"] = sum(fps_values) / len(fps_values)
                
                # 提取bitrate (bitrate= 1234.5kbits/s)
                bitrate_matches = re.findall(r'bitrate=\s*(\d+\.?\d*)kbits/s', content)
                if bitrate_matches:
                    bitrate_values = [float(x) for x in bitrate_matches if x]
                    if bitrate_values:
                        metrics["bitrate_kbps"] = sum(bitrate_values) / len(bitrate_values)
                
                # 检测帧丢失
                drop_matches = re.findall(r'drop.*?(\d+)', content, re.IGNORECASE)
                if drop_matches:
                    metrics["frames_dropped"] = sum(int(x) for x in drop_matches)
                
                # 检测错误
                error_lines = [line for line in content.split('\n') if 'error' in line.lower()]
                metrics["errors"] = error_lines[:10]  # 最多保存10条
                
        except Exception as e:
            print(f"Warning: Failed to parse {log_file}: {e}")
        
        return metrics
    
    def extract_client_metrics(self, log_file: Path) -> Dict[str, Any]:
        """从客户端日志中提取XQUIC指标"""
        metrics = {
            "connection_time_ms": 0,
            "bytes_received": 0,
            "packets_received": 0,
            "packets_lost": 0,
            "retransmits": 0
        }
        
        if not log_file.exists():
            return metrics
        
        try:
            with open(log_file, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                
                # TODO: 根据实际XQUIC日志格式提取指标
                # 这里需要根据你的camera_client的日志输出来定制
                
                # 示例：查找连接建立时间
                conn_match = re.search(r'connected.*?(\d+)ms', content, re.IGNORECASE)
                if conn_match:
                    metrics["connection_time_ms"] = int(conn_match.group(1))
                
                # 查找接收字节数
                bytes_match = re.search(r'received.*?(\d+)\s*bytes', content, re.IGNORECASE)
                if bytes_match:
                    metrics["bytes_received"] = int(bytes_match.group(1))
                    
        except Exception as e:
            print(f"Warning: Failed to parse {log_file}: {e}")
        
        return metrics
    
    def analyze_scenario(self, scenario_name: str, scenario_dir: Path) -> Dict[str, Any]:
        """分析单个场景"""
        print(f"Analyzing: {scenario_name}")
        
        # 读取基本配置
        metrics_file = scenario_dir / "metrics.json"
        with open(metrics_file, 'r') as f:
            base_metrics = json.load(f)
        
        # 分析日志
        logs_dir = scenario_dir / "logs"
        
        # UDP流（左侧）指标
        udp_metrics = self.extract_ffmpeg_metrics(logs_dir / "decoder_left.log")
        
        # XQUIC流（右侧）指标
        xquic_decoder_metrics = self.extract_ffmpeg_metrics(logs_dir / "decoder_right.log")
        xquic_client_metrics = self.extract_client_metrics(logs_dir / "client.log")
        
        # 合并结果
        result = {
            "scenario": scenario_name,
            "network_config": base_metrics.get("network_config", {}),
            "duration": base_metrics.get("actual_duration_seconds", 0),
            "udp_stream": {
                "fps": udp_metrics["fps"],
                "bitrate_kbps": udp_metrics["bitrate_kbps"],
                "frames_dropped": udp_metrics["frames_dropped"],
                "error_count": len(udp_metrics["errors"])
            },
            "xquic_stream": {
                "fps": xquic_decoder_metrics["fps"],
                "bitrate_kbps": xquic_decoder_metrics["bitrate_kbps"],
                "frames_dropped": xquic_decoder_metrics["frames_dropped"],
                "error_count": len(xquic_decoder_metrics["errors"]),
                "connection_time_ms": xquic_client_metrics["connection_time_ms"],
                "bytes_received": xquic_client_metrics["bytes_received"]
            }
        }
        
        return result
    
    def analyze_all(self):
        """分析所有场景"""
        for scenario_name, scenario_dir in self.scenarios:
            try:
                result = self.analyze_scenario(scenario_name, scenario_dir)
                self.metrics[scenario_name] = result
            except Exception as e:
                print(f"Error analyzing {scenario_name}: {e}")
                import traceback
                traceback.print_exc()
    
    def generate_report(self, output_file: str = None):
        """生成分析报告"""
        if not self.metrics:
            print("No metrics to report")
            return
        
        # 生成纯文本报告
        report_lines = []
        report_lines.append("=" * 80)
        report_lines.append("XQUIC Performance Analysis Report")
        report_lines.append("=" * 80)
        report_lines.append("")
        
        # 场景对比表
        report_lines.append("Performance Comparison")
        report_lines.append("-" * 80)
        
        header = f"{'Scenario':<25} {'Latency':<10} {'Loss':<8} {'UDP FPS':<10} {'XQUIC FPS':<12} {'Improvement'}"
        report_lines.append(header)
        report_lines.append("-" * 80)
        
        for scenario_name in sorted(self.metrics.keys()):
            data = self.metrics[scenario_name]
            network = data["network_config"]
            
            latency = f"{network.get('latency_ms', 0)}ms"
            loss = f"{network.get('packet_loss_pct', 0)}%"
            udp_fps = data["udp_stream"]["fps"]
            xquic_fps = data["xquic_stream"]["fps"]
            
            if udp_fps > 0:
                improvement = ((xquic_fps - udp_fps) / udp_fps) * 100
                improvement_str = f"{improvement:+.1f}%"
            else:
                improvement_str = "N/A"
            
            line = f"{scenario_name:<25} {latency:<10} {loss:<8} {udp_fps:<10.1f} {xquic_fps:<12.1f} {improvement_str}"
            report_lines.append(line)
        
        report_lines.append("-" * 80)
        report_lines.append("")
        
        # 详细指标
        report_lines.append("Detailed Metrics")
        report_lines.append("=" * 80)
        
        for scenario_name in sorted(self.metrics.keys()):
            data = self.metrics[scenario_name]
            report_lines.append("")
            report_lines.append(f"Scenario: {scenario_name}")
            report_lines.append("-" * 40)
            
            network = data["network_config"]
            report_lines.append(f"  Network Config:")
            report_lines.append(f"    Latency: {network.get('latency_ms', 0)}ms")
            report_lines.append(f"    Packet Loss: {network.get('packet_loss_pct', 0)}%")
            report_lines.append(f"    Bandwidth: {network.get('bandwidth_mbps', 0)}Mbps")
            report_lines.append("")
            
            report_lines.append(f"  UDP Stream:")
            report_lines.append(f"    FPS: {data['udp_stream']['fps']:.1f}")
            report_lines.append(f"    Bitrate: {data['udp_stream']['bitrate_kbps']:.1f} kbps")
            report_lines.append(f"    Frames Dropped: {data['udp_stream']['frames_dropped']}")
            report_lines.append(f"    Errors: {data['udp_stream']['error_count']}")
            report_lines.append("")
            
            report_lines.append(f"  XQUIC Stream:")
            report_lines.append(f"    FPS: {data['xquic_stream']['fps']:.1f}")
            report_lines.append(f"    Bitrate: {data['xquic_stream']['bitrate_kbps']:.1f} kbps")
            report_lines.append(f"    Frames Dropped: {data['xquic_stream']['frames_dropped']}")
            report_lines.append(f"    Errors: {data['xquic_stream']['error_count']}")
            report_lines.append(f"    Connection Time: {data['xquic_stream']['connection_time_ms']}ms")
            report_lines.append("")
        
        report_lines.append("=" * 80)
        
        # 输出报告
        report_text = "\n".join(report_lines)
        print(report_text)
        
        # 保存到文件
        if output_file:
            with open(output_file, 'w') as f:
                f.write(report_text)
            print(f"\nReport saved to: {output_file}")
        
        # 保存JSON格式
        json_file = output_file.replace('.txt', '.json') if output_file else None
        if json_file:
            with open(json_file, 'w') as f:
                json.dump(self.metrics, f, indent=2)
            print(f"JSON data saved to: {json_file}")


def main():
    parser = argparse.ArgumentParser(description='Analyze XQUIC experiment results')
    parser.add_argument('results_dir', help='Results directory to analyze')
    parser.add_argument('-o', '--output', help='Output report file', default=None)
    
    args = parser.parse_args()
    
    analyzer = ExperimentAnalyzer(args.results_dir)
    
    if not analyzer.scan_scenarios():
        print("No scenarios found to analyze")
        return 1
    
    analyzer.analyze_all()
    
    # 生成输出文件名
    if args.output:
        output_file = args.output
    else:
        output_file = str(Path(args.results_dir) / "analysis_report.txt")
    
    analyzer.generate_report(output_file)
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
