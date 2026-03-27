#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <vector>
#include <atomic>
#include <semaphore>
#include <unordered_map>

struct Task {
    int id;
    int required_slots;
    int duration_ms;
    int priority;

    bool operator<(const Task &other) const { return priority < other.priority; }
};

class TaskScheduler {
public:
    TaskScheduler(const int &total_slots, const int &num_workers)
        : resource_semaphore(total_slots)
          , completed_tasks(0)
          , done(false) {
        for (int i = 0; i < num_workers; ++i) {
            workers.emplace_back([this] { worker(); });
        }
    }

    ~TaskScheduler() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            done = true;
        }
        cv.notify_all();
        for (auto &t: workers)
            t.join();
    }

    void submit(const Task &task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            tasks.push(task);
            waiting_times[task.id] = std::chrono::steady_clock::now();
        }
        cv.notify_one();
    }

private:
    std::priority_queue<Task> tasks;
    std::counting_semaphore<> resource_semaphore;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::vector<std::thread> workers;
    std::atomic<int> completed_tasks;
    bool done;
    std::unordered_map<int, std::chrono::steady_clock::time_point> waiting_times;
    static std::mutex cout_mutex;

    void worker() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                cv.wait(lock, [this] { return !tasks.empty() || done; });
                if (done && tasks.empty())
                    break;
                task = tasks.top();
                tasks.pop();
            }

            for (int i = 0; i < task.required_slots; ++i)
                resource_semaphore.acquire();

            execute_task(task);

            for (int i = 0; i < task.required_slots; ++i)
                resource_semaphore.release();

            ++completed_tasks;

            auto now = std::chrono::steady_clock::now();
            const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - waiting_times[task.id]).
                    count();
            {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << "Worker " << std::this_thread::get_id()
                        << " finished task " << task.id
                        << " (waited " << wait_ms << " ms)" << std::endl;
            }
            std::this_thread::yield();
        }
    }

    static void execute_task(const Task &task) {
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Worker " << std::this_thread::get_id()
                    << " executing task " << task.id
                    << " (slots " << task.required_slots << ")"
                    << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(task.duration_ms));
    }
};

std::mutex TaskScheduler::cout_mutex;

int main() {
    TaskScheduler scheduler(4, 3);

    std::vector<std::thread> submitters;
    for (int i = 0; i < 10; ++i) {
        submitters.emplace_back([&scheduler, i] {
            Task t{i, (i % 3) + 1, 100, (i % 5) + 1};
            scheduler.submit(t);
            std::this_thread::yield();
        });
    }

    for (auto &t: submitters) {
        t.join();
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
    return 0;
}
