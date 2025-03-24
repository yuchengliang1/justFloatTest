import socket
import struct
import time
import pandas as pd
import numpy as np
from pathlib import Path

class EEGDataSender:
    def __init__(self, 
                 csv_path: str,
                 fs: int = 1024,              # 采样率
                 target_port: int = 9999,     # 目标端口
                 target_ip: str = '127.0.0.1' # 目标IP
                 ):
        self.fs = fs
        self.sample_interval = 1.0 / fs  # 采样间隔
        
        # UDP设置
        self.target_address = (target_ip, target_port)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        
        # 加载数据
        self.data = self._load_data(csv_path)
        self.total_samples = len(self.data)
        
        # 统计信息
        self.sent_packets = 0
        self.start_time = None

    def _load_data(self, csv_path: str) -> np.ndarray:
        """加载CSV数据文件"""
        try:
            # 读取CSV文件
            df = pd.read_csv(csv_path)
            
            # 假设CSV文件的每一列是一个通道的数据
            data = df.values
            
            print(f"Loaded data shape: {data.shape}")
            print(f"Number of channels: {data.shape[1]}")
            
            return data
        except Exception as e:
            raise Exception(f"Error loading CSV file: {e}")

    def _create_justfloat_packet(self, sample_data: np.ndarray) -> bytes:
        """创建JustFloat格式的数据包"""
        packet = bytearray()
        
        # 添加每个通道的数据
        for value in sample_data:
            packet.extend(struct.pack('<f', float(value)))
            
        # 添加JustFloat帧尾
        packet.extend(b'\x00\x00\x80\x7f')
        
        return packet

    def _maintain_timing(self, start_time: float, sample_number: int):
        """维持发送时序"""
        expected_time = start_time + (sample_number * self.sample_interval)
        current_time = time.time()
        
        if current_time < expected_time:
            time.sleep(expected_time - current_time)

    def start_streaming(self, loop: bool = True):
        """开始数据流传输"""
        print(f"Starting data streaming at {self.fs}Hz")
        print(f"Target address: {self.target_address}")
        
        try:
            while True:  # 外循环用于数据重播
                self.start_time = time.time()
                self.sent_packets = 0
                
                # 遍历所有数据样本
                for i in range(self.total_samples):
                    # 获取当前样本数据
                    sample = self.data[i]
                    
                    # 创建JustFloat数据包
                    packet = self._create_justfloat_packet(sample)
                    
                    # 发送数据
                    self.socket.sendto(packet, self.target_address)
                    self.sent_packets += 1
                    
                    # 维持采样率
                    self._maintain_timing(self.start_time, i)
                    
                    # 每秒打印一次状态信息
                    if i % self.fs == 0:
                        elapsed_time = time.time() - self.start_time
                        print(f"Sent {self.sent_packets} packets, "
                              f"Time: {elapsed_time:.2f}s, "
                              f"Sample: {i}/{self.total_samples}")
                
                if not loop:
                    break
                    
                print("Data streaming completed. Restarting...")
                
        except KeyboardInterrupt:
            print("\nStreaming stopped by user")
        except Exception as e:
            print(f"Error during streaming: {e}")
        finally:
            self.socket.close()
            print("Socket closed")

    @staticmethod
    def generate_test_csv(filename: str, 
                         duration: float = 10.0,    # 数据时长(秒)
                         num_channels: int = 32,     # 通道数
                         fs: int = 1024,           # 采样率
                         ):
        """生成测试用的CSV文件"""
        # 计算样本数
        num_samples = int(duration * fs)
        
        # 生成测试数据
        time_points = np.linspace(0, duration, num_samples)
        data = {}
        
        # 为每个通道生成不同频率的正弦波
        for ch in range(num_channels):
            freq = (ch + 1) * 10  # 不同通道使用不同频率
            data[f'Channel_{ch}'] = np.sin(2 * np.pi * freq * time_points)
        
        # 创建DataFrame并保存
        df = pd.DataFrame(data)
        df.to_csv(filename, index=False)
        print(f"Generated test file: {filename}")
        print(f"Duration: {duration}s, Channels: {num_channels}, Sampling rate: {fs}Hz")

# 使用示例
if __name__ == "__main__":
    # 1. 首先生成测试数据文件
    test_file = r"C:\Users\yu\Desktop\JustFloatTest\test_eeg_data.csv"
    # EEGDataSender.generate_test_csv(
    #     filename=test_file,
    #     duration=10.0,      # 10秒数据
    #     num_channels=32,     # 32个通道
    #     fs=1000            # 1000Hz采样率
    # )
    
    # 2. 创建发送器实例
    sender = EEGDataSender(
        csv_path=test_file,
        fs=1024,           # 1024Hz采样率
        target_port=9999,  # 目标端口
        target_ip='127.0.0.1'  # 目标IP
    )
    
    # 3. 开始发送数据
    sender.start_streaming(loop=True)  # loop=True表示循环发送