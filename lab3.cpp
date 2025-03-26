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

using namespace std;
using namespace std::chrono;

// Глобальний mutex для синхронізації виводу в консоль
mutex cout_mutex;

class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();

    // Додає задачу в пул
    void addTask(const function<void()>& task);

    // Призупиняє виконання задач
    void pause();
    // Відновлює виконання задач
    void resume();

    // Завершення роботи пулу
    // immediate = true: миттєве завершення (відкидання невиконаних задач)
    // immediate = false: граціозне завершення (виконання всіх задач)
    void shutdown(bool immediate);

    // Вивід зібраних метрик
    void printMetrics();

private:
    // Внутрішня структура для черги задач
    struct TaskQueue {
        queue<function<void()>> tasks;
        mutex mtx;
        condition_variable cv;
        bool stop = false;
        bool paused = false;
    };

    // 3 черги, кожна з яких реалізована через unique_ptr
    vector<unique_ptr<TaskQueue>> queues;
    // Вектор робочих потоків
    vector<thread> workers;

    // М'ютекс для безпечного додавання задач
    mutex addTaskMutex;

    // Метрики – загальний час очікування, кількість вимірювань,
    // а також кількість створених і завершених задач
    mutex metricsMutex;
    microseconds totalWaitTime{0};
    size_t waitCount = 0;
    atomic<size_t> totalTasksCreated{0};
    atomic<size_t> totalTasksCompleted{0};

    // Функція, яку виконують робочі потоки
    void workerFunction(int queueIndex);
};

ThreadPool::ThreadPool() {
    // Ініціалізуємо 3 черги
    for (int i = 0; i < 3; ++i) {
        queues.push_back(make_unique<TaskQueue>());
    }

    // Створюємо 2 робочих потоки для кожної черги (всього 6 потоків)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            workers.emplace_back(&ThreadPool::workerFunction, this, i);
        }
    }
}

ThreadPool::~ThreadPool() {
    // Використовуємо миттєве завершення, щоб зупинити всі задачі
    shutdown(true);
    for (auto &worker : workers) {
        if (worker.joinable())
            worker.join();
    }
}

void ThreadPool::workerFunction(int queueIndex) {
    TaskQueue &q = *queues[queueIndex];
    while (true) {
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
        if (q.stop && q.tasks.empty()) {
            break;
        }
        if (!q.tasks.empty()) {
            auto task = q.tasks.front();
            q.tasks.pop();
            lock.unlock();
            task();
            // Після виконання задачі збільшуємо лічильник завершених задач
            ++totalTasksCompleted;
        }
    }
}

void ThreadPool::addTask(const function<void()>& task) {
    lock_guard<mutex> lock(addTaskMutex);
    int minIndex = 0;
    size_t minSize = SIZE_MAX;
    // Обираємо чергу з найменшою кількістю задач
    for (int i = 0; i < 3; ++i) {
        lock_guard<mutex> qlock(queues[i]->mtx);
        size_t qSize = queues[i]->tasks.size();
        if (qSize < minSize) {
            minSize = qSize;
            minIndex = i;
        }
    }
    {
        lock_guard<mutex> qlock(queues[minIndex]->mtx);
        queues[minIndex]->tasks.push(task);
    }
    queues[minIndex]->cv.notify_one();
    // Збільшуємо лічильник створених задач
    ++totalTasksCreated;
}

void ThreadPool::pause() {
    for (auto &q_ptr : queues) {
        lock_guard<mutex> lock(q_ptr->mtx);
        q_ptr->paused = true;
    }
}

void ThreadPool::resume() {
    for (auto &q_ptr : queues) {
        {
            lock_guard<mutex> lock(q_ptr->mtx);
            q_ptr->paused = false;
        }
        q_ptr->cv.notify_all();
    }
}

void ThreadPool::shutdown(bool immediate) {
    for (auto &q_ptr : queues) {
        lock_guard<mutex> lock(q_ptr->mtx);
        if (immediate) {
            // Відкидаємо всі невиконані задачі
            while (!q_ptr->tasks.empty()) {
                q_ptr->tasks.pop();
            }
            q_ptr->stop = true;
        } else {
            // Граціозне завершення – дозволяємо виконати всі задачі
            q_ptr->stop = true;
        }
        q_ptr->cv.notify_all();
    }
}

void ThreadPool::printMetrics() {
    lock_guard<mutex> lock(metricsMutex);
    {
        lock_guard<mutex> lockOut(cout_mutex);
        if (waitCount > 0) {
            auto avgWait = totalWaitTime.count() / waitCount;
            cout << "Average thread waiting time: " << avgWait << " microseconds" << endl;
        } else {
            cout << "No waiting time measured." << endl;
        }
        cout << "Number of threads created: " << workers.size() << endl;
        cout << "Total tasks created: " << totalTasksCreated.load() << endl;
        cout << "Total tasks completed: " << totalTasksCompleted.load() << endl;
    }
}

//
// Simulated task – задача, що виконується від 8 до 14 секунд
//
void simulatedTask(int taskId) {
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

//
// Функція main – приклад використання пулу потоків
//
int main() {
    ThreadPool pool;

    // Час тестування (наприклад, 30 секунд)
    auto testDuration = seconds(30);
    auto testEnd = steady_clock::now() + testDuration;

    // Продюсерські потоки, що додають задачі кожні 500 мс
    vector<thread> producers;
    atomic<int> taskCounter{0};
    auto producerFunc = [&pool, &testEnd, &taskCounter]() {
        while (steady_clock::now() < testEnd) {
            int id = ++taskCounter;
            pool.addTask([id]() {
                simulatedTask(id);
            });
            this_thread::sleep_for(milliseconds(500));
        }
    };

    int numProducers = 3;
    for (int i = 0; i < numProducers; ++i) {
        producers.emplace_back(producerFunc);
    }

    // Таймер для повного завершення роботи програми через testDuration
    thread timerThread([&pool, testDuration]() {
        this_thread::sleep_for(testDuration);
        // Використовуємо миттєве завершення, щоб зупинити всі задачі
        pool.shutdown(true);
    });

    for (auto &p : producers) {
        if (p.joinable())
            p.join();
    }

    if (timerThread.joinable())
        timerThread.join();

    pool.printMetrics();

    return 0;
}
