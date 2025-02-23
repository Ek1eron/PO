#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <thread>

using namespace std;

void generateMatrix(vector<vector<int>>& matrix, int size) //one thread function
{
    for (int i = 0; i < size; i++)
        {
        int evenSum = 0;
        for (int j = 0; j < size; j++)
            {
            matrix[i][j] = rand() % 100;
            if (j % 2 == 0)
                {
                evenSum += matrix[i][j];
            }
        }
        matrix[i][i] = evenSum;
    }
}

void HelpgenerateMatrixMulti(vector<vector<int>>& matrix, int start, int end, int size)
{
    for (int i = start; i < end; i++)
        {
        int evenSum = 0;
        for (int j = 0; j < size; j++)
            {
            matrix[i][j] = rand() % 100;
            if (j % 2 == 0) {
                evenSum += matrix[i][j];
            }
        }
        matrix[i][i] = evenSum;
    }
}

void generateMatrixMulti(vector<vector<int>>& matrix, int size, int m) // Multi-thread function
{
    vector<thread> threads;
    int rows_num = size / m; //rows num for each thread
    int remainder = size % m;
    int start = 0;

    for (int t = 0; t < m; t++)
        {
        int end = start + rows_num + (t < remainder ? 1 : 0);
        threads.emplace_back(HelpgenerateMatrixMulti, ref(matrix), start, end, size);
        start = end;
    }

    for (auto& th : threads)
        {
        th.join();
    }
}

void printMatrix(const vector<vector<int>>& matrix)
{
    for (const auto& row : matrix)
        {
        for (int val : row)
            {
            cout << val << " ";
        }
        cout << endl;
    }
}

int main()
{
    vector<int> thread_counts = {3, 6, 12, 24, 48, 96}; // *0.5, *1, *2, *4,*8,*16
    vector<int> matrix_sizes = {100, 500, 1000, 2000, 5000, 10000};

    for (int size : matrix_sizes)
        {
        cout << "\nMatrix size: " << size << "\n";
        vector<vector<int>> matrix(size, vector<int>(size));

        auto start_time = chrono::high_resolution_clock::now();
        generateMatrix(matrix, size);
        auto end_time = chrono::high_resolution_clock::now();
        chrono::duration<double> ex1_time = end_time - start_time;

        cout << "1 thread Execution Time: " << fixed << setprecision(6) << ex1_time.count() << " seconds" << endl;

        for (int m : thread_counts)
            {
            vector<vector<int>> matrix_multi(size, vector<int>(size));
            start_time = chrono::high_resolution_clock::now();
            generateMatrixMulti(matrix_multi, size, m);
            end_time = chrono::high_resolution_clock::now();
            chrono::duration<double> exm_time = end_time - start_time;

            cout << "Parallel Execution Time (" << m << " threads): " << fixed << setprecision(6) << exm_time.count() << " seconds" << endl;
        }
    }

    return 0;
}
