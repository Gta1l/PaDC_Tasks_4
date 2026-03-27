#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <vector>
#include <atomic>
#include <semaphore>
#include <functional>

struct PrintJob {
    std::string doc;
    int priority;
    std::thread::id id;
    bool interrupted;
    int job_id;
};

class PrinterQueue {
public:
    explicit PrinterQueue(const int &n)
        : n_printers(n)
          , semaphore(n)
          , next_id(0) {
    }

    void printJob(const std::string &doc, const int &priority, const int &timeout_ms) {
        const int job_id = next_id++;

        if (semaphore.try_acquire()) {
            doPrint(doc, priority, job_id);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            waiting.push({doc, priority, std::this_thread::get_id(), false, job_id});
        }

        std::unique_lock<std::mutex> lock(mtx);
        const auto &job = findJob();
        if (!job.interrupted && cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                            [&] { return job.interrupted || semaphore.try_acquire(); })) {
            if (job.interrupted) {
                waiting.push(job);
                print(job, "interrupted, back to queue");
                lock.unlock();
            } else {
                lock.unlock();
                doPrint(doc, priority, job_id);
            }
        } else {
            print(job, "timeout");
            lock.unlock();
        }
    }

private:
    int n_printers;
    std::counting_semaphore<> semaphore;
    std::mutex mtx;
    std::condition_variable cv;
    std::priority_queue<PrintJob, std::vector<PrintJob>,
        std::function<bool(const PrintJob &, const PrintJob &)> > waiting{
        [](const PrintJob &a, const PrintJob &b) { return a.priority < b.priority; }
    };
    std::atomic<int> next_id;

    void doPrint(const std::string &doc, const int &priority, const int &job_id) {
        const PrintJob job{doc, priority, std::this_thread::get_id(), false, job_id};
        print(job, "started");
        for (int i = 0; i < 10; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            {
                std::lock_guard<std::mutex> lock(mtx);
                if (!waiting.empty() && waiting.top().priority > priority) {
                    semaphore.release();
                    print(job, "interrupted");
                    waiting.push({doc, priority, std::this_thread::get_id(), true, job_id});
                    return;
                }
            }
        }
        semaphore.release();
        print(job, "finished");
    }

    static PrintJob &findJob() {
        static PrintJob job;
        return job;
    }

    static void print(const PrintJob &job, const std::string &status) {
        static std::mutex cout_mtx;
        std::lock_guard<std::mutex> lock(cout_mtx);
        std::cout << "Thread " << job.id
                << " (job " << job.job_id << ", priority " << job.priority << "): "
                << status << std::endl;
    }
};

int main() {
    PrinterQueue pq(2);
    std::vector<std::thread> threads;

    auto job = [&pq](const std::string &doc, const int &prio, const int &ms) {
        pq.printJob(doc, prio, ms);
        std::this_thread::yield();
    };

    threads.emplace_back(job, "Doc1", 5, 200);
    threads.emplace_back(job, "Doc2", 1, 200);
    threads.emplace_back(job, "Doc3", 10, 200);
    threads.emplace_back(job, "Doc4", 8, 200);

    for (auto &t: threads) {
        t.join();
    }

    return 0;
}
