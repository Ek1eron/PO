#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <chrono>
#include <random>
#include <memory>
#include <atomic>
#include <limits>

using namespace std;
using namespace std::chrono;

mutex cout_mutex;

class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();

    void addTask(const function<void()>& task);

    void pause();
    void resume();

    // immediate = true: миттєве завершення
    // immediate = false: плавне завершення
    void shutdown(bool immediate);

    void printMetrics();

private:
    struct TaskQueue
    {
        queue<function<void()>> tasks;
        mutex mtx;
        condition_variable cv;
        bool stop = false;
        bool paused = false;
        size_t totalQueueLength = 0;
        size_t measurements = 0;
        size_t maxQueueLength = 0;
        size_t minQueueLength = std::numeric_limits<size_t>::max();
    };

    vector<unique_ptr<TaskQueue>> queues;
    vector<thread> workers;

    mutex addTaskMutex;

    mutex metricsMutex;
    microseconds totalWaitTime{0};
    size_t waitCount = 0;
    atomic<size_t> totalTasksCreated{0};
    atomic<size_t> totalTasksCompleted{0};
    atomic<size_t> totalTaskExecutionTime{0};

    void workerFunction(int queueIndex);
};

ThreadPool::ThreadPool()
{
    for (int i = 0; i < 3; ++i)
    {
        queues.push_back(make_unique<TaskQueue>());
    }

    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            workers.emplace_back(&ThreadPool::workerFunction, this, i);
        }
    }
}

ThreadPool::~ThreadPool()
{
    shutdown(true);
    for (auto &worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }
}

void ThreadPool::workerFunction(int queueIndex)
{
    TaskQueue &q = *queues[queueIndex];
    while (true)
    {
        unique_lock<mutex> lock(q.mtx);
        auto wait_start = steady_clock::now();
        q.cv.wait(lock, [&]{
            return (!q.paused && !q.tasks.empty()) || (q.stop && q.tasks.empty());
        });
        auto wait_end = steady_clock::now();
        {
            lock_guard<mutex> mlock(metricsMutex);
            totalWaitTime += duration_cast<microseconds>(wait_end - wait_start);
            ++waitCount;
        }
        if (q.stop && q.tasks.empty())
        {
            break;
        }
        if (!q.tasks.empty())
        {
            auto task = q.tasks.front();
            q.tasks.pop();
            lock.unlock();
            auto task_start = steady_clock::now();
            task();
            auto task_end = steady_clock::now();
            totalTaskExecutionTime += duration_cast<microseconds>(task_end - task_start).count();
            ++totalTasksCompleted;
        }
    }
}

void ThreadPool::addTask(const function<void()>& task)
{
    lock_guard<mutex> lock(addTaskMutex);
    int minIndex = 0;
    size_t minSize = SIZE_MAX;
    for (int i = 0; i < 3; ++i)
    {
        lock_guard<mutex> qlock(queues[i]->mtx);
        size_t qSize = queues[i]->tasks.size();
        if (qSize < minSize)
        {
            minSize = qSize;
            minIndex = i;
        }
    }
    {
        lock_guard<mutex> qlock(queues[minIndex]->mtx);
        queues[minIndex]->tasks.push(task);
        size_t currentLength = queues[minIndex]->tasks.size();
        queues[minIndex]->totalQueueLength += currentLength;
        ++queues[minIndex]->measurements;
        if (currentLength > queues[minIndex]->maxQueueLength)
            queues[minIndex]->maxQueueLength = currentLength;
        if (currentLength < queues[minIndex]->minQueueLength)
            queues[minIndex]->minQueueLength = currentLength;
    }
    queues[minIndex]->cv.notify_one();
    ++totalTasksCreated;
}

void ThreadPool::pause()
{
    for (auto &q_ptr : queues)
    {
        lock_guard<mutex> lock(q_ptr->mtx);
        q_ptr->paused = true;
    }
}

void ThreadPool::resume()
{
    for (auto &q_ptr : queues)
    {
        {
            lock_guard<mutex> lock(q_ptr->mtx);
            q_ptr->paused = false;
        }
        q_ptr->cv.notify_all();
    }
}

void ThreadPool::shutdown(bool immediate)
{
    for (auto &q_ptr : queues)
    {
        lock_guard<mutex> lock(q_ptr->mtx);
        if (immediate)
        {
            while (!q_ptr->tasks.empty())
            {
                q_ptr->tasks.pop();
            }
            q_ptr->stop = true;
        }
        else
        {
            q_ptr->stop = true;
        }
        q_ptr->cv.notify_all();
    }
}

void ThreadPool::printMetrics()
{
    lock_guard<mutex> lock(metricsMutex);
    {
        lock_guard<mutex> lockOut(cout_mutex);
        if (waitCount > 0)
        {
            auto avgWait = totalWaitTime.count() / waitCount;
            cout << "Average thread waiting time: " << avgWait << " microseconds" << endl;
        }
        else
        {
            cout << "No waiting time measured." << endl;
        }
        cout << "Number of threads created: " << workers.size() << endl;
        cout << "Total tasks created: " << totalTasksCreated.load() << endl;
        cout << "Total tasks completed: " << totalTasksCompleted.load() << endl;
        if (totalTasksCompleted > 0)
        {
            auto avgExecTime = totalTaskExecutionTime.load() / totalTasksCompleted.load();
            cout << "Average task execution time: " << avgExecTime << " microseconds" << endl;
        }
        for (size_t i = 0; i < queues.size(); ++i)
        {
            auto &q = queues[i];
            lock_guard<mutex> qlock(q->mtx);
            if (q->measurements > 0) {
                size_t avgQueueLength = q->totalQueueLength / q->measurements;
                cout << "Queue " << i << " average length: " << avgQueueLength;
                cout << ", max length: " << q->maxQueueLength;
                cout << ", min length: " << q->minQueueLength << endl;
            }
            else
            {
                cout << "Queue " << i << " has no measurements." << endl;
            }
        }
    }
}

void simulatedTask(int taskId)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(8, 14);
    int execTime = dis(gen);
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "Task " << taskId << " is starting, expected time: "
             << execTime << " seconds." << endl;
    }
    this_thread::sleep_for(seconds(execTime));
    {
        lock_guard<mutex> lock(cout_mutex);
        cout << "Task " << taskId << " completed." << endl;
    }
}

int main()
{
    ThreadPool pool;

    auto testDuration = seconds(30);
    auto testEnd = steady_clock::now() + testDuration;

    vector<thread> producers;
    atomic<int> taskCounter{0};
    auto producerFunc = [&pool, &testEnd, &taskCounter]()
    {
        random_device rd;
        mt19937 gen(rd());
        // Випадковий інтервал від 1000 до 5000 мілісекунд (1-5 секунд)
        uniform_int_distribution<> dis(1000, 5000);

        while (steady_clock::now() < testEnd)
        {
            int id = ++taskCounter;
            pool.addTask([id]()
            {
                simulatedTask(id);
            });
            this_thread::sleep_for(milliseconds(dis(gen)));
        }
    };


    int numProducers = 3;
    for (int i = 0; i < numProducers; ++i)
    {
        producers.emplace_back(producerFunc);
    }

    thread timerThread([&pool, testDuration]()
    {
        this_thread::sleep_for(testDuration);
        pool.shutdown(true);
    });

    for (auto &p : producers)
    {
        if (p.joinable())
            p.join();
    }

    if (timerThread.joinable())
        timerThread.join();

    pool.printMetrics();

    return 0;
}
