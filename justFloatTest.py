import socket
import struct
import numpy as np
from collections import deque
from threading import Thread, Lock
from queue import Queue
import time

class EEGProcessor:
    def __init__(self, 
                 window_size=1.0,     # 处理窗口大小(秒)
                 slide_size=0.5,      # 滑动窗口步长(秒)
                 channel_num=2,       # 通道数
                 fs=1000):           # 采样率
        # 基础配置
        self.channel_num = channel_num
        self.fs = fs
        
        # 计算基于采样点的窗口大小和滑动步长
        self.window_points = int(window_size * fs)
        self.slide_points = int(slide_size * fs)
        
        # 数据缓冲区 - 保存足够长的数据以供滑动窗口使用
        self.data_buffer = {
            i: deque(maxlen=self.window_points*2) for i in range(channel_num)
        }
        self.buffer_lock = Lock()
        
        # 处理队列
        self.process_queue = Queue(maxsize=100)
        
        # UDP配置
        self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_socket.bind(('127.0.0.1', 9999))
        
        # 控制标志
        self.running = False
        
        # 计数器 - 用于追踪接收的样本数
        self.sample_count = 0
        
        # 存储处理结果
        self.classification_results = Queue()
        
        # 时间戳
        self.start_time = None
        
        print(f"Initialized with window size: {window_size}s ({self.window_points} points)")
        print(f"Slide size: {slide_size}s ({self.slide_points} points)")

    def start(self):
        """启动数据采集和处理"""
        self.running = True
        self.start_time = time.time()
        self.sample_count = 0
        
        # 启动数据采集线程
        self.acquisition_thread = Thread(target=self._data_acquisition)
        self.acquisition_thread.daemon = True
        self.acquisition_thread.start()
        
        # 启动数据处理线程
        self.processing_thread = Thread(target=self._data_processing)
        self.processing_thread.daemon = True
        self.processing_thread.start()

    def stop(self):
        """停止数据采集和处理"""
        self.running = False
        self.acquisition_thread.join()
        self.processing_thread.join()

    def _data_acquisition(self):
        """数据采集线程"""
        while self.running:
            try:
                # 接收UDP数据
                data, addr = self.udp_socket.recvfrom(1024)
                
                # 解析JustFloat协议数据
                frame_data = self._parse_justfloat(data)
                
                # 更新缓冲区和计数器
                with self.buffer_lock:
                    for ch in range(self.channel_num):
                        self.data_buffer[ch].append(frame_data[ch])
                    self.sample_count += 1
                
                # 检查是否需要处理
                if self.sample_count >= self.slide_points:
                    self._check_and_create_window()
                    
            except Exception as e:
                print(f"Data acquisition error: {e}")

    def _check_and_create_window(self):
        """检查并创建处理窗口"""
        with self.buffer_lock:
            if len(self.data_buffer[0]) >= self.window_points:
                # 创建处理窗口
                window_data = self._create_processing_window()
                self.process_queue.put({
                    'timestamp': time.time() - self.start_time,
                    'data': window_data
                })
                # 重置计数器
                self.sample_count = 0

    def _parse_justfloat(self, data):
        """解析JustFloat协议数据"""
        channel_data = []
        for i in range(self.channel_num):
            start_idx = i * 4
            value = struct.unpack('<f', data[start_idx:start_idx+4])[0]
            channel_data.append(value)
        return channel_data

    def _create_processing_window(self):
        """创建用于处理的数据窗口"""
        window_data = np.zeros((self.channel_num, self.window_points))
        for ch in range(self.channel_num):
            # 获取最近的window_points个数据点
            data = list(self.data_buffer[ch])[-self.window_points:]
            window_data[ch, :len(data)] = data
        return window_data

    def _data_processing(self):
        """数据处理线程"""
        while self.running:
            try:
                # 从处理队列获取数据
                window_package = self.process_queue.get(timeout=1)
                timestamp = window_package['timestamp']
                window_data = window_package['data']
                
                # 预处理
                processed_data = self._preprocess(window_data)
                
                # 特征提取
                features = self._extract_features(processed_data)
                
                # 分类
                result = self._classify(features)
                
                # 添加时间戳
                result['timestamp'] = timestamp
                
                # 存储结果
                self.classification_results.put(result)
                
            except Exception as e:
                if 'queue.Empty' not in str(e):
                    print(f"Data processing error: {e}")

    def _preprocess(self, data):
        """数据预处理"""
        # 示例预处理步骤
        # 1. 去均值
        data = data - np.mean(data, axis=1, keepdims=True)
        # 2. 这里可以添加滤波等其他预处理步骤
        return data

    def _extract_features(self, data):
        """特征提取"""
        # 在这里实现你的特征提取方法
        features = {
            'mean': np.mean(data, axis=1),
            'std': np.std(data, axis=1),
            'max': np.max(data, axis=1),
            'min': np.min(data, axis=1)
        }
        return features

    def _classify(self, features):
        """分类接口"""
        # 在这里实现你的深度学习模型调用
        return {
            "features": features,
            "class": None,
            "probability": None
        }

    def get_latest_result(self):
        """获取最新的分类结果"""
        if not self.classification_results.empty():
            return self.classification_results.get()
        return None

# 使用示例
if __name__ == "__main__":
    # 创建处理器实例：1秒窗口，0.5秒滑动步长
    processor = EEGProcessor(
        window_size=1.0,    # 1秒的处理窗口
        slide_size=0.5,     # 0.5秒的滑动步长
        channel_num=2,      # 2个通道
        fs=1000            # 1000Hz采样率
    )
    
    try:
        # 启动处理
        processor.start()
        
        # 主循环
        while True:
            # 获取最新结果
            result = processor.get_latest_result()
            if result:
                print(f"Time: {result['timestamp']:.2f}s, Classification result: {result['class']}")
            time.sleep(0.1)
            
    except KeyboardInterrupt:
        # 优雅退出
        processor.stop()