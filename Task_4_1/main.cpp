#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <queue>
#include <semaphore>
#include <random>
#include <future>
#include <algorithm>

template<typename T>
class ResourcePool {
public:
    explicit ResourcePool(std::vector<T> &&res)
        : resources(std::move(res))
          , semaphore(resources.size())
          , failed_attempts(0) {
        for (const auto &r: resources)
            freeResources.push(r);
    }

    T acquire(const int &priority, const int &timeout_ms) {
        std::unique_lock<std::mutex> lock(mtx);

        if (semaphore.try_acquire()) {
            T res = getFreeResource();
            lock.unlock();
            print("acquire", priority, true);
            return res;
        }

        auto promise = std::make_shared<std::promise<T> >();
        auto future = promise->get_future();
        waiters.push_back({priority, promise, std::this_thread::get_id()});
        std::sort(waiters.begin(), waiters.end(),
                  [](const Waiter &a, const Waiter &b) { return a.priority > b.priority; });

        lock.unlock();

        auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
        if (status == std::future_status::ready) {
            T res = future.get();
            print("acquire", priority, true);
            return res;
        } else {
            lock.lock();
            auto it = std::find_if(waiters.begin(), waiters.end(),
                                   [](const Waiter &w) { return w.id == std::this_thread::get_id(); });
            if (it != waiters.end())
                waiters.erase(it);
            ++failed_attempts;
            lock.unlock();
            print("acquire", priority, false);
            throw std::runtime_error("Timeout acquiring resource");
        }
    }

    void release(const T &res) {
        std::lock_guard<std::mutex> lock(mtx);
        freeResources.push(res);
        semaphore.release();

        if (!waiters.empty()) {
            auto highest = waiters.front();
            waiters.erase(waiters.begin());
            highest.promise->set_value(getFreeResource());
        }
        print("release", -1, true);
    }

    int getFailedAttempts() const {
        return failed_attempts.load();
    }

private:
    std::vector<T> resources;
    std::queue<T> freeResources;
    std::counting_semaphore<> semaphore;
    std::mutex mtx;
    std::atomic<int> failed_attempts;

    struct Waiter {
        int priority;
        std::shared_ptr<std::promise<T> > promise;
        std::thread::id id;
    };

    std::vector<Waiter> waiters;

    T getFreeResource() {
        if (freeResources.empty()) {
            throw std::runtime_error("No free resource");
        }
        T res = freeResources.front();
        freeResources.pop();
        return res;
    }

    void print(const std::string &action, int priority, bool success) {
        static std::mutex cout_mtx;
        std::lock_guard<std::mutex> lock(cout_mtx);
        std::cout << "Thread " << std::this_thread::get_id()
                << " " << action
                << " (priority " << priority << ") "
                << (success ? "succeeded" : "failed")
                << std::endl;
    }
};

int main() {
    ResourcePool<int> pool({1, 2, 3});

    auto worker = [&pool](const int &priority) {
        try {
            std::this_thread::sleep_for(std::chrono::milliseconds(rand() % 100));
            const int res = pool.acquire(priority, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            pool.release(res);
        } catch (const std::exception &) {
        }
        std::this_thread::yield();
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        int priority = (i % 2 == 0) ? 10 : 1;
        threads.emplace_back(worker, priority);
    }

    for (auto &t: threads) {
        t.join();
    }

    std::cout << "Failed attempts: " << pool.getFailedAttempts() << std::endl;
    return 0;
}
