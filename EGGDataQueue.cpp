// EGGDataQueue.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <vector>
#include <boost/asio.hpp>

using boost::asio::ip::udp;


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
    char data_[163840];
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

void udpConsumer(std::queue<float>& dataQueue, EEGOneSecondBuffer& buffer, std::mutex& mtx) {
    const int timeWindow = 1;
    const int Frequency = 1000;
    const int Channel = 32;
    std::vector<float> tempVector;
    
    while (true) {
        if (dataQueue.size() >= timeWindow * Frequency * Channel)
        {
            {
                std::cout << dataQueue.size() / (Frequency * Channel) - timeWindow << "seconds data delayed!!!" << std::endl;
                std::lock_guard<std::mutex> lock(mtx);
                for (int i = 0; i < timeWindow * Frequency; i++) {
                    tempVector.clear();
                    for (int j = 0; j < Channel; j++) {
                        if (!dataQueue.empty()) {
                            tempVector.push_back(dataQueue.front());
                            dataQueue.pop();
                        }
                    }
                    buffer.add(tempVector);
                }
            }
            RealTimeClassifier cls(buffer.getData());
            cls.performClassification();
        }

    }
}
int main() {
    std::mutex mtx;
    std::queue<float> dataQueue;
    const int channelCount = 32;
    EEGOneSecondBuffer buffer(channelCount);
 
    std::thread t1(udpReceiver, std::ref(dataQueue), std::ref(mtx));
    std::thread t2(udpConsumer, std::ref(dataQueue), std::ref(buffer), std::ref(mtx));

    t1.join();
    t2.join();

    return 0;
}
