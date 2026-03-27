#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <semaphore>
#include <future>
#include <algorithm>

class ParkingLot {
public:
    explicit ParkingLot(const int &cap)
        : capacity(cap)
          , semaphore(cap)
          , occupied(0) {
    }

    bool park(const bool &isVIP, const int &timeout_ms) {
        std::unique_lock<std::mutex> lock(mtx);
        if (semaphore.try_acquire()) {
            ++occupied;
            lock.unlock();
            print(isVIP, true);
            return true;
        }

        const auto promise = std::make_shared<std::promise<void> >();
        const auto future = promise->get_future();
        waitersVec.push_back({isVIP, promise, std::this_thread::get_id()});
        std::sort(waitersVec.begin(), waitersVec.end(),
                  [](const Waiter &a, const Waiter &b) { return a.isVIP > b.isVIP; });

        lock.unlock();

        auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
        if (status == std::future_status::ready) {
            lock.lock();
            ++occupied;
            lock.unlock();
            print(isVIP, true);
            return true;
        } else {
            lock.lock();
            const auto it = std::find_if(waitersVec.begin(), waitersVec.end(),
                                         [](const Waiter &w) { return w.id == std::this_thread::get_id(); });
            if (it != waitersVec.end())
                waitersVec.erase(it);
            lock.unlock();
            print(isVIP, false);
            return false;
        }
    }

    void leave() {
        std::lock_guard<std::mutex> lock(mtx);
        --occupied;
        semaphore.release();

        if (!waitersVec.empty()) {
            const auto it = waitersVec.begin();
            it->promise->set_value();
            waitersVec.erase(it);
        }
        print(false, true);
    }

    void setCapacity(const int &newCap) {
        std::lock_guard<std::mutex> lock(mtx);
        const int delta = newCap - capacity;
        capacity = newCap;
        if (delta > 0) {
            for (int i = 0; i < delta; ++i)
                semaphore.release();
        } else if (delta < 0) {
            for (int i = 0; i < -delta; ++i)
                semaphore.acquire();
        }
    }

private:
    int capacity;
    std::counting_semaphore<> semaphore;
    int occupied;
    std::mutex mtx;

    struct Waiter {
        bool isVIP;
        std::shared_ptr<std::promise<void> > promise;
        std::thread::id id;
    };

    std::vector<Waiter> waitersVec;

    void print(const bool &isVIP, const bool &success) const {
        static std::mutex cout_mtx;
        std::lock_guard<std::mutex> lock(cout_mtx);
        std::cout << "Thread " << std::this_thread::get_id()
                << " " << (isVIP ? "VIP" : "regular")
                << " car " << (success ? "parked" : "failed")
                << " | occupied=" << occupied << " free=" << (capacity - occupied)
                << std::endl;
    }
};

int main() {
    ParkingLot lot(4);
    std::vector<std::thread> cars;

    auto car = [&lot](const bool &vip) {
        const bool parked = lot.park(vip, 50);
        if (parked) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            lot.leave();
        }
        std::this_thread::yield();
    };

    for (int i = 0; i < 6; ++i) {
        cars.emplace_back(car, i % 2 == 0);
    }

    for (auto &t: cars) {
        t.join();
    }

    return 0;
}
