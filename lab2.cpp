#include <iostream>
#include <vector>
#include <climits>
#include <chrono>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <limits>

using namespace std;

const vector<int> DATA_SIZES = {10000, 50000, 100000, 500000, 1000000};
const vector<int> THREAD_COUNTS = {3, 6, 12, 24, 48, 96};
vector<int> arr;

void generate_data(int size)
{
    static random_device rd;
    static mt19937 gen(rd());
    uniform_int_distribution<int> dist(-1000000, 1000000);

    arr.resize(size);
    for (int &num : arr)
    {
        num = dist(gen);
    }
}

void sequential_find(int &count, int &min_negative)
{
    count = 0;
    min_negative = INT_MAX;
    for (int i = 0; i < arr.size(); ++i)
    {
        if (arr[i] < 0)
        {
            count++;
            if (arr[i] < min_negative)
                min_negative = arr[i];
        }
    }
}

void work_mutex(int start, int end, int &count, int &min_negative, mutex &mtx)
{
    for (int i = start; i < end; ++i)
    {
        if (arr[i] < 0)
        {
            lock_guard<mutex> lock(mtx);
            count++;
            if (arr[i] < min_negative)
                min_negative = arr[i];
        }
    }
}

void parallel_find_mutex(int &count, int &min_negative, int num_threads)
{
    count = 0;
    min_negative = INT_MAX;
    mutex mtx;
    vector<thread> threads;
    int base_chunk_size = arr.size() / num_threads;
    int remainder = arr.size() % num_threads;
    int start = 0;

    for (int i = 0; i < num_threads; ++i)
    {
        int chunk_size = base_chunk_size + (i < remainder ? 1 : 0);
        int end = start + chunk_size;
        threads.emplace_back(work_mutex, start, end, std::ref(count), std::ref(min_negative), std::ref(mtx));
        start = end;
    }
    for (auto &t : threads)
    {
        t.join();
    }
}

void work_atomic(int start, int end, atomic<int> &count, atomic<int> &min_negative)
{
    for (int i = start; i < end; ++i)
    {
        if (arr[i] < 0)
        {
            count.fetch_add(1, memory_order_relaxed);
            int current = min_negative.load(memory_order_relaxed);
            while (arr[i] < current && !min_negative.compare_exchange_weak(current, arr[i], memory_order_relaxed))
            {
            }
        }
    }
}

void parallel_find_atomic(atomic<int> &count, atomic<int> &min_negative, int num_threads)
{
    count = 0;
    min_negative = INT_MAX;
    vector<thread> threads;
    int base_chunk_size = arr.size() / num_threads;
    int remainder = arr.size() % num_threads;
    int start = 0;

    for (int i = 0; i < num_threads; ++i)
    {
        int chunk_size = base_chunk_size + (i < remainder ? 1 : 0);
        int end = start + chunk_size;
        threads.emplace_back(work_atomic, start, end, std::ref(count), std::ref(min_negative));
        start = end;
    }
    for (auto &t : threads)
    {
        t.join();
    }
}

int main()
{
    srand(time(NULL));

    for (size_t i = 0; i < DATA_SIZES.size(); ++i)
    {
        int size = DATA_SIZES[i];
        generate_data(size);

        int seq_count, seq_min;
        atomic<int> atomic_count, atomic_min;

        auto start = chrono::high_resolution_clock::now();
        sequential_find(seq_count, seq_min);
        auto end = chrono::high_resolution_clock::now();
        cout << "Linear (Single Thread) - Data size: " << size << " - Execution time: "
             << fixed << setprecision(6) << chrono::duration<double>(end - start).count() << " sec\n"
             << "Negative count: " << seq_count << ", Minimum negative: " << seq_min << "\n\n";

        for (int num_threads : THREAD_COUNTS)
        {
            int mtx_count, mtx_min;
            start = chrono::high_resolution_clock::now();
            parallel_find_mutex(mtx_count, mtx_min, num_threads);
            end = chrono::high_resolution_clock::now();
            cout << "Mutex Parallel - Threads: " << num_threads << " - Data size: " << size << " - Execution time: "
                 << fixed << setprecision(6) << chrono::duration<double>(end - start).count() << " sec\n"
                 << "Negative count: " << mtx_count << ", Minimum negative: " << mtx_min << "\n\n";
            start = chrono::high_resolution_clock::now();
            parallel_find_atomic(atomic_count, atomic_min, num_threads);
            end = chrono::high_resolution_clock::now();
            cout << "Atomic CAS Parallel - Threads: " << num_threads << " - Data size: " << size << " - Execution time: "
                 << fixed << setprecision(6) << chrono::duration<double>(end - start).count() << " sec\n"
                 << "Negative count: " << atomic_count << ", Minimum negative: " << atomic_min << "\n\n";
        }
    }

    return 0;
}
