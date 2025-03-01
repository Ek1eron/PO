#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>

using namespace std;

const vector<int> DATA_SIZES = {10000, 50000, 100000, 500000, 1000000};
vector<int> arr;

void generate_data(int size)
{
    arr.resize(size);
    for (int &num : arr)
    {
        num = (rand() % 10001) - 5000;
    }
}

void sequential_find(int &count, int &min_negative)
{
    count = 0;
    min_negative = 0;
    for (int num : arr)
        {
        if (num < 0)
        {
            count++;
            if (num < min_negative)
            {
                min_negative = num;
            }
        }
    }
}

int main()
{
    srand(time(NULL));

    for (size_t i = 0; i < DATA_SIZES.size(); ++i)
        {
        int size = DATA_SIZES[i];
        generate_data(size);

        if (i == 0)
            {
            cout << "Generated array: ";
            for (int num : arr)
                {
                cout << num << " ";
            }
            cout << "\n\n";
        }

        int count, min_negative;
        auto start = chrono::high_resolution_clock::now();
        sequential_find(count, min_negative);
        auto end = chrono::high_resolution_clock::now();

        cout << "Data size: " << size << " - Execution time: "
             << fixed << setprecision(6)
             << chrono::duration<double>(end - start).count() << " sec\n"
             << "Negative count: " << count << ", Minimum negative: " << min_negative << "\n\n";
    }

    return 0;
}
