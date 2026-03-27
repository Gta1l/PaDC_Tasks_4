#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <semaphore>
#include <memory>

template<typename T>
class SemaphoreBuffer {
public:
    SemaphoreBuffer(const int &num_buffers, const int &buffer_size) {
        for (int i = 0; i < num_buffers; ++i) {
            buffers.emplace_back();
            buffers.back().reserve(buffer_size);
            empty.push_back(std::make_unique<std::counting_semaphore<> >(buffer_size));
            full.push_back(std::make_unique<std::counting_semaphore<> >(0));
            mtx.push_back(std::make_unique<std::mutex>());
        }
    }

    void produce(const T &value, const int &buffer_index, const int &timeout_ms) {
        if (!tryProduce(value, buffer_index, timeout_ms)) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, buffers.size() - 1);
            for (int attempt = 0; attempt < 5; ++attempt) {
                if (int idx = dis(gen); idx != buffer_index && tryProduce(value, idx, timeout_ms))
                    return;
            }
            print("produce", buffer_index, value, false, timeout_ms);
        }
    }

    T consume(const int &buffer_index, const int &timeout_ms) {
        T value;
        if (tryConsume(value, buffer_index, timeout_ms))
            return value;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, buffers.size() - 1);
        for (int attempt = 0; attempt < 5; ++attempt) {
            int idx = dis(gen);
            if (idx != buffer_index && tryConsume(value, idx, timeout_ms))
                return value;
        }
        print("consume", buffer_index, value, false, timeout_ms);
        return T{};
    }

private:
    std::vector<std::vector<T> > buffers;
    std::vector<std::unique_ptr<std::counting_semaphore<> > > empty;
    std::vector<std::unique_ptr<std::counting_semaphore<> > > full;
    std::vector<std::unique_ptr<std::mutex> > mtx;

    bool tryProduce(const T &value, const int &idx, const int &timeout_ms) {
        if (empty[idx]->try_acquire_for(std::chrono::milliseconds(timeout_ms))) {
            std::lock_guard<std::mutex> lock(*mtx[idx]);
            buffers[idx].push_back(value);
            full[idx]->release();
            print("produce", idx, value, true, timeout_ms);
            return true;
        }
        return false;
    }

    bool tryConsume(T &value, const int &idx, const int &timeout_ms) {
        if (full[idx]->try_acquire_for(std::chrono::milliseconds(timeout_ms))) {
            std::lock_guard<std::mutex> lock(*mtx[idx]);
            value = buffers[idx].back();
            buffers[idx].pop_back();
            empty[idx]->release();
            print("consume", idx, value, true, timeout_ms);
            return true;
        }
        return false;
    }

    static void print(const std::string &action, const int &idx, const T &val, const bool &success, const int &timeout) {
        static std::mutex cout_mtx;
        std::lock_guard<std::mutex> lock(cout_mtx);
        std::cout << "Thread " << std::this_thread::get_id()
                << " " << action << " buffer " << idx
                << " value " << val
                << " " << (success ? "OK" : "timeout after " + std::to_string(timeout) + "ms")
                << std::endl;
    }
};

int main() {
    SemaphoreBuffer<int> sb(3, 5);
    std::vector<std::thread> producers, consumers;

    auto producer = [&sb](const int &id) {
        for (int i = 0; i < 5; ++i) {
            sb.produce(id * 10 + i, id % 3, 50);
            std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 100));
            std::this_thread::yield();
        }
    };

    auto consumer = [&sb](const int &id) {
        for (int i = 0; i < 5; ++i) {
            int val = sb.consume(id % 3, 50);
            std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 100));
            std::this_thread::yield();
        }
    };

    for (int i = 0; i < 4; ++i) {
        producers.emplace_back(producer, i);
    }
    for (int i = 0; i < 4; ++i) {
        consumers.emplace_back(consumer, i);
    }

    for (auto &t: producers) {
        t.join();
    }
    for (auto &t: consumers) {
        t.join();
    }

    return 0;
}
