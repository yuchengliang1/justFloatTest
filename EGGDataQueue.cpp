// EGGDataQueue.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <vector>
#include <onnxruntime_cxx_api.h>
#include <boost/asio.hpp>
#include <chrono>
#include <windows.h>

using boost::asio::ip::udp;
#pragma comment(lib, "onnxruntime.lib")  // ONNX Runtime库

bool setThreadPriority(std::thread& t, int priority) {
    HANDLE handle = (HANDLE)t.native_handle();
    return SetThreadPriority(handle, priority) != 0;
}

template <typename T>
static void softmax(T& input) {
    float rowmax = *std::max_element(input.begin(), input.end());
    std::vector<float> y(input.size());
    float sum = 0.0f;
    for (size_t i = 0; i != input.size(); ++i) {
        sum += y[i] = std::exp(input[i] - rowmax);
    }
    for (size_t i = 0; i != input.size(); ++i) {
        input[i] = y[i] / sum;
    }
}

struct EEGDataModel {
    // 构造函数，初始化ONNX Runtime相关组件
    EEGDataModel() {
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
        input_tensor_ = Ort::Value::CreateTensor<float>(memory_info, input_image_.data(), input_image_.size(),
            input_shape_.data(), input_shape_.size());
        output_tensor_ = Ort::Value::CreateTensor<float>(memory_info, results_.data(), results_.size(),
            output_shape_.data(), output_shape_.size());
    }
    std::ptrdiff_t Run() {
        // 定义输入输出节点的名称，必须与模型中定义的名称一致
        const char* input_names[] = { "input" };
        const char* output_names[] = { "output" };
        Ort::TypeInfo type_info = session_.GetInputTypeInfo(0);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
        std::vector<int64_t> input_dims = tensor_info.GetShape();

        // 打印期望的输入维度
        //std::cout << "Expected input dimensions: ";
        //for (auto& dim : input_dims) {
        //    std::cout << dim << " ";
        //}
        //std::cout << std::endl;
        try {
            Ort::RunOptions run_options;
            auto start = std::chrono::high_resolution_clock::now();
            session_.Run(run_options, input_names, &input_tensor_, 1, output_names, &output_tensor_, 1);
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed = end - start;
            double inference_time_ms = elapsed.count();
            std::cout << "Inference status : success" << std::endl << "Inference time : " << inference_time_ms << " ms" << std::endl;
        }
        catch (const Ort::Exception& e) {
            // 打印异常信息
            std::cerr << "Inference status : error" << e.what() << std::endl;
            return 1;
        }
        softmax(results_);
        result_ = std::distance(results_.begin(), std::max_element(results_.begin(), results_.end()));
        bool first = true;
        for (const auto& element : results_) {
            if (!first) {
                std::cout << " ";
            }
            std::cout << element;
            first = false;
        }
        std::cout << std::endl;
        std::cout << std::endl;
        return result_;
    }
    static const int channelCount = 32;
    static const int frequency = 1024;
    static const int timeWindow = 1;

    // 输入图像数据，存储为一维浮点数组 （32,1024）
    std::array<float, channelCount* frequency* timeWindow > input_image_{};
    // 输出结果，6个值对应6分类的概率
    std::array<float, 6> results_{};
    int64_t result_{ 0 };

private:
    Ort::Env env;
    Ort::Session session_{ env, L"model.onnx", Ort::SessionOptions{nullptr} };
    Ort::Value input_tensor_{ nullptr };
    std::array<int64_t, 3> input_shape_{ 1, channelCount, frequency * timeWindow };
    Ort::Value output_tensor_{ nullptr };
    std::array<int64_t, 3> output_shape_{ 1, 6, 1 };
};

class EEGOneSecondBuffer {
private:
    std::vector<std::vector<float>> eegData;
    std::vector<float> tempVector;
    int channelCount;

public:
    EEGOneSecondBuffer(int channel) : channelCount(channel) {}
    bool add(const std::vector<float>& data) {
        if (data.size() != static_cast<size_t>(channelCount)) {
            std::cerr << "Error: The number of data points does not match the channel count." << std::endl;
            return false;
        }
        eegData.push_back(data);
        return true;
    }
    const std::vector<std::vector<float>>& getData() const {
        return eegData;
    }
    int getChannelCount() const {
        return channelCount;
    }
    size_t getRecordCount() const {
        return eegData.size();
    }
    void clear() {
        eegData.clear();
    }
};

class UDPDataReceiver {
public:
    UDPDataReceiver(boost::asio::io_context& io_context, std::queue<float>& sharedData, std::mutex& mtx) : socket_(io_context, udp::endpoint(udp::v4(), 9999)), dataQueue_(sharedData), mtx_(mtx) {
        startReceive();
    }
private:
    void startReceive() {
        socket_.async_receive_from(
            boost::asio::buffer(data_), sender_endpoint_,
            [this](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    processPacket(length);
                    startReceive();
                }
            });
    }
    void processPacket(std::size_t length) {
        if (length < 4) {
            return;
        }
        // std::cout << "收到 " << length << " 字节的数据" << std::endl; // 12字节
        if (std::memcmp(data_ + length - 4, frame_end, 4) == 0) {
            std::size_t data_length = length - 4;
            std::size_t float_count = data_length / sizeof(float);

            std::lock_guard<std::mutex> lock(mtx_);
            for (std::size_t i = 0; i < float_count; ++i) {
                float value;
                std::memcpy(&value, data_ + i * sizeof(float), sizeof(float));
                dataQueue_.push(value);
            }
        }
    }
    udp::socket socket_;
    udp::endpoint sender_endpoint_;
    char data_[396];
    const unsigned char frame_end[4] = { 0x00, 0x00, 0x80, 0x7f };
    std::queue<float>& dataQueue_;
    std::mutex& mtx_;
};

class RealTimeClassifier {
private:
    std::vector<std::vector<float>> inputData;
    std::vector<int> classificationResults;
    int classify(const std::vector<float>& sample) {
        if (sample[0] > 0.5) {
            return 1;
        }
        else {
            return 0;
        }
    }

public:
    RealTimeClassifier(const std::vector<std::vector<float>>& data) : inputData(data) {}
    void performClassification() {
        classificationResults.clear();
        for (const auto& sample : inputData) {
            int result = classify(sample);
            classificationResults.push_back(result);
        }
        inputData.clear();
        std::cout << "classify ended" << std::endl;
    }
    const std::vector<int>& getClassificationResults() const {
        return classificationResults;
    }
};


void udpReceiver(std::queue<float>& sharedData, std::mutex& mtx) {
    boost::asio::io_context io_context;
    UDPDataReceiver udpDataReceiver(io_context, sharedData, mtx);
    io_context.run();
}

void udpConsumer(std::queue<float>& dataQueue, std::mutex& mtx) {
    const int timeWindow = 1;
    const int Frequency = 1024;
    const int Channel = 32;
    // std::vector<float> tempVector;
    std::unique_ptr<EEGDataModel> model_;
    
    try {
        model_ = std::make_unique<EEGDataModel>();
    }
    catch (const Ort::Exception& exception) {
        // 打印异常信息到标准错误流
        std::cerr << "ONNX Runtime 异常: " << exception.what() << std::endl;
    }
    
    while (true) {
        if (dataQueue.size() >= timeWindow * Frequency * Channel)
        {
            {
                float* output = model_->input_image_.data();
                std::fill(model_->input_image_.begin(), model_->input_image_.end(), 0.f);
                if (dataQueue.size() / (Frequency * Channel) - timeWindow >= 1) {
                    std::cout << dataQueue.size() / (Frequency * Channel) - timeWindow << " seconds data delayed!!!" << std::endl;
                }
                std::lock_guard<std::mutex> lock(mtx);
                //for (int i = 0; i < timeWindow * Frequency; i++) {
                //    // tempVector.clear();
                //    for (int j = 0; j < Channel; j++) {
                //        if (!dataQueue.empty()) {
                //            // tempVector.push_back(dataQueue.front());

                //            dataQueue.pop();
                //        }
                //    }
                //    // buffer.add(tempVector);
                //}
                for (int i = 0; i < timeWindow * Frequency * Channel; i++) {
                    if (!dataQueue.empty()) {
                        output[i] = dataQueue.front();
                        dataQueue.pop();
                    }
                }

            }
            model_->Run();
        }
    }

}




int main() {
    std::mutex mtx;
    std::queue<float> dataQueue;
    const int channelCount = 32;

    std::thread t1(udpReceiver, std::ref(dataQueue), std::ref(mtx));
    std::thread t2(udpConsumer, std::ref(dataQueue), std::ref(mtx));

    setThreadPriority(t1, THREAD_PRIORITY_HIGHEST);
    setThreadPriority(t2, THREAD_PRIORITY_HIGHEST);

    t1.join();
    t2.join();

    return 0;
}
