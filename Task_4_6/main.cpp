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
#include <unordered_set>

struct FileChunk {
    int chunk_id;
    int file_id;
    size_t size;

    void download() const {
        std::this_thread::sleep_for(std::chrono::milliseconds(size));
    }
};

class FileDownload {
public:
    int file_id;
    std::vector<FileChunk> chunks;
    std::atomic<int> downloaded_chunks;

    FileDownload(int id, const std::vector<FileChunk> &chks)
        : file_id(id), chunks(chks), downloaded_chunks(0) {
    }

    bool is_complete() const {
        return downloaded_chunks == static_cast<int>(chunks.size());
    }

    void mark_chunk_downloaded() {
        ++downloaded_chunks;
    }
};

class DownloadManager {
public:
    DownloadManager(const int &max_active_files, const int &max_chunks)
        : active_downloads(max_active_files),
          chunk_downloads(max_chunks),
          completed_files(0),
          stop(false) {
        for (int i = 0; i < max_chunks; ++i) {
            std::thread([this] { worker(); }).detach();
        }
    }

    ~DownloadManager() {
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            stop = true;
        }
        cv.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    void add_file(const FileDownload &file) {
        auto [it, inserted] = file_downloads.try_emplace(file.file_id,
                                                         file.file_id,
                                                         file.chunks);
        for (const auto &chunk: it->second.chunks) {
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                chunk_queue.push(chunk);
            }
        }
        cv.notify_one();
    }

    void wait_for_completion() const {
        while (completed_files < static_cast<int>(file_downloads.size())) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

private:
    std::queue<FileChunk> chunk_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;

    std::counting_semaphore<> active_downloads;
    std::counting_semaphore<> chunk_downloads;
    std::atomic<int> completed_files;

    std::unordered_map<int, FileDownload> file_downloads;
    std::unordered_set<int> active_files;
    std::mutex state_mutex;

    bool stop;

    static std::mutex cout_mutex;

    void worker() {
        while (true) {
            FileChunk chunk;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                cv.wait(lock, [this] { return !chunk_queue.empty() || stop; });
                if (stop && chunk_queue.empty()) break;
                chunk = chunk_queue.front();
                chunk_queue.pop();
            }

            chunk_downloads.acquire();

            bool need_acquire = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (!active_files.contains(chunk.file_id)) {
                    need_acquire = true;
                }
            }
            if (need_acquire) {
                active_downloads.acquire();
                std::lock_guard<std::mutex> lock(state_mutex);
                if (!active_files.contains(chunk.file_id)) {
                    active_files.insert(chunk.file_id);
                } else {
                    active_downloads.release();
                }
            }

            process_chunk(chunk);

            chunk_downloads.release();

            bool file_complete = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                auto it = file_downloads.find(chunk.file_id);
                if (it != file_downloads.end()) {
                    it->second.mark_chunk_downloaded();
                    if (it->second.is_complete()) {
                        file_complete = true;
                        active_files.erase(chunk.file_id);
                    }
                }
            }
            if (file_complete) {
                active_downloads.release();
                ++completed_files;
            }

            std::this_thread::yield();
        }
    }

    static inline void process_chunk(const FileChunk &chunk) {
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Worker " << std::this_thread::get_id()
                    << " downloading file " << chunk.file_id
                    << " chunk " << chunk.chunk_id
                    << " (size " << chunk.size << " ms)" << std::endl;
        }
        chunk.download();
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << "Worker " << std::this_thread::get_id()
                    << " finished file " << chunk.file_id
                    << " chunk " << chunk.chunk_id << std::endl;
        }
    }
};

std::mutex DownloadManager::cout_mutex;

int main() {
    DownloadManager manager(2, 3);

    for (int f = 1; f <= 4; ++f) {
        std::vector<FileChunk> chunks;
        for (int c = 0; c < 5; ++c) {
            chunks.push_back({c, f, static_cast<size_t>(50 + c * 10)});
        }
        manager.add_file(FileDownload(f, chunks));
    }

    manager.wait_for_completion();
    std::cout << "All files downloaded successfully." << std::endl;
    return 0;
}
