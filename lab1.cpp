#include <iostream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <iomanip>

using namespace std;

void generateMatrix(vector<vector<int>>& matrix, int size)
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
    int n;
    cout << "Enter matrix size: ";
    cin >> n;

    vector<vector<int>> matrix(n, vector<int>(n));

    auto start_time = chrono::high_resolution_clock::now();
    generateMatrix(matrix, n);
    auto end_time = chrono::high_resolution_clock::now();

    chrono::duration<double> execution_time = end_time - start_time;
    cout << fixed << setprecision(6);
    cout << "Executing time: " << execution_time.count() << " seconds" << endl;

    printMatrix(matrix);
    return 0;
}