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

void work(int start, int end, int &local_count, int &local_min)
{
    local_count = 0;
    local_min = INT_MAX;
    for (int i = start; i < end; ++i)
    {
        if (arr[i] < 0)
        {
            local_count++;
            if (arr[i] < local_min)
            {
                local_min = arr[i];
            }
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

    vector<int> local_counts(num_threads, 0);
    vector<int> local_mins(num_threads, INT_MAX);

    int start = 0;
    for (int i = 0; i < num_threads; ++i)
    {
        int chunk_size = base_chunk_size + (i < remainder ? 1 : 0);
        int end = start + chunk_size;
        threads.emplace_back(work, start, end, ref(local_counts[i]), ref(local_mins[i]));
        start = end;
    }
    for (auto &t : threads)
    {
        t.join();
    }

    for (int i = 0; i < num_threads; ++i)
    {
        lock_guard<mutex> lock(mtx);
        count += local_counts[i];
        if (local_mins[i] < min_negative)
        {
            min_negative = local_mins[i];
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

        threads.emplace_back([&, start, end]()
        {
            int local_count = 0;
            int local_min = INT_MAX;
            work(start, end, local_count, local_min);
            count.fetch_add(local_count, memory_order_relaxed);
            int expected = min_negative.load();
            while (local_min < expected && !min_negative.compare_exchange_weak(expected, local_min, memory_order_relaxed));
        });
        start = end;
    }
    for (auto &t : threads)
    {
        t.join();
    }
}


void sequential_find(int &count, int &min_negative)
{
    work(0, arr.size(), count, min_negative);
}

int main()
{
    srand(time(NULL));

    for (size_t i = 0; i < DATA_SIZES.size(); ++i)
    {
        int size = DATA_SIZES[i];
        generate_data(size);

        int count, min_negative;
        atomic<int> atomic_count, atomic_min_negative;

        auto start = chrono::high_resolution_clock::now();
        sequential_find(count, min_negative);
        auto end = chrono::high_resolution_clock::now();
        cout << "Linear (Single Thread) - Data size: " << size << " - Execution time: "
             << fixed << setprecision(6) << chrono::duration<double>(end - start).count() << " sec\n"
             << "Negative count: " << count << ", Minimum negative: " << min_negative << "\n\n";

        for (int num_threads : THREAD_COUNTS)
        {
            start = chrono::high_resolution_clock::now();
            parallel_find_mutex(count, min_negative, num_threads);
            end = chrono::high_resolution_clock::now();
            cout << "Mutex Parallel - Threads: " << num_threads << " - Data size: " << size << " - Execution time: "
                 << fixed << setprecision(6) << chrono::duration<double>(end - start).count() << " sec\n"
                 << "Negative count: " << count << ", Minimum negative: " << min_negative << "\n\n";

            start = chrono::high_resolution_clock::now();
            parallel_find_atomic(atomic_count, atomic_min_negative, num_threads);
            end = chrono::high_resolution_clock::now();
            cout << "Atomic CAS Parallel - Threads: " << num_threads << " - Data size: " << size << " - Execution time: "
                 << fixed << setprecision(6) << chrono::duration<double>(end - start).count() << " sec\n"
                 << "Negative count: " << atomic_count << ", Minimum negative: " << atomic_min_negative << "\n\n";
        }
    }

    return 0;
}