#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <new>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

using namespace std;

constexpr size_t PAIR_COUNT = 640; 
// пары узлов размещаются на двух уровнях кеша, так если все узлы находятся только в первом уровне, то время доступа к одной строке c AB будет таким же как к двум строкам A и B
constexpr size_t SLOT_SIZE = 384; // байт на пару; 128*3=384
constexpr size_t WARM = 4;
constexpr size_t TRIALS = 10;
constexpr size_t MIN_ACCESSES = 5'000'000;

struct Node // каждый узел 8 байт
{
    Node *next;
};

// пара узлов находится в отдельной области с выравниванием 128 байт
struct alignas(128) Slot // область памяти размером 384 байта в которой размещаются два Node с определенным расстоянием между ними
{
    unsigned char storage[SLOT_SIZE];
};

// сохраняем результат обхода, чтобы вычисления не были удалены оптимизатором
volatile uintptr_t saved_result = 0;

struct Statistics
{
    double median;
    double mean;
    double stddev;
    double cv;
};

Statistics calculate_statistics(vector<double> times)
{
    sort(times.begin(), times.end());
    const double median = times[times.size() / 2];
    const double mean = accumulate(times.begin(), times.end(), 0.0) / times.size();
    double variance = 0.0;
    for (double value : times)
    {
        variance += (value - mean) * (value - mean);
    }
    variance /= times.size();
    const double stddev = sqrt(variance);
    const double cv = mean > 0.0 ? stddev / mean * 100.0 : 0.0;
    return {median, mean, stddev, cv};
}

Node *pointer_chase(Node *current, size_t steps) noexcept
{
    for (size_t i = 0; i < steps; ++i)
    {
        current = current->next;
    }
    return current;
}

bool validate_cycle(const vector<Node *> &first, const vector<Node *> &second, const vector<size_t> &order, Node *start)
{
    Node *current = start;
    for (size_t i = 0; i < order.size(); ++i)
    {
        const size_t index = order[i];
        const size_t next_index = order[(i + 1) % order.size()];
        if (current != first[index])
        {
            return false;
        }
        current = current->next;
        if (current != second[index])
        {
            return false;
        }
        current = current->next;
        if (current != first[next_index])
        {
            return false;
        }
    }
    return current == start;
}

Statistics measure(Node *start, size_t cycle_steps)
{
    using Clock = chrono::steady_clock;
    const size_t cycles = (MIN_ACCESSES + cycle_steps - 1) / cycle_steps;
    const size_t steps = cycles * cycle_steps;
    vector<double> times;
    times.reserve(TRIALS);

    for (size_t trial = 0; trial < TRIALS; ++trial)
    {
        for (size_t warm = 0; warm < WARM; ++warm)
        {
            Node *result = pointer_chase(start, cycle_steps);
            saved_result = reinterpret_cast<uintptr_t>(result);
            if (result != start)
            {
                throw runtime_error("Ошибка проверки цикла при прогреве");
            }
        }

        const auto time_start = Clock::now();
        Node *result = pointer_chase(start, steps);
        const auto time_end = Clock::now();
        saved_result = reinterpret_cast<uintptr_t>(result);
        if (result != start)
        {
            throw runtime_error("Ошибка проверки цикла при измерении");
        }
        const double elapsed_ns = chrono::duration<double, nano>(time_end - time_start).count();
        times.push_back(elapsed_ns / steps);
    }
    return calculate_statistics(times);
}

Statistics run_experiment(size_t distance, const vector<size_t> &order)
{
    if (distance < sizeof(Node) || distance > SLOT_SIZE - sizeof(Node) || distance % alignof(Node) != 0)
    {
        throw invalid_argument("Недопустимое расстояние между узлами");
    }

    vector<Slot> slots(PAIR_COUNT);
    vector<Node *> first_nodes(PAIR_COUNT);
    vector<Node *> second_nodes(PAIR_COUNT);
    for (size_t i = 0; i < PAIR_COUNT; ++i)
    {
        first_nodes[i] = ::new (static_cast<void *>(slots[i].storage)) Node{nullptr};
        second_nodes[i] = ::new (static_cast<void *>(slots[i].storage + distance)) Node{nullptr};
    }

    for (size_t i = 0; i < PAIR_COUNT; ++i)
    {
        const size_t index = order[i];
        const size_t next_index = order[(i + 1) % PAIR_COUNT];
        first_nodes[index]->next = second_nodes[index];
        second_nodes[index]->next = first_nodes[next_index];
    }

    Node *start = first_nodes[order[0]];
    if (!validate_cycle(first_nodes, second_nodes, order, start))
    {
        throw runtime_error("Ошибка проверки построенного цикла");
    }
    return measure(start, PAIR_COUNT * 2);
}

int main()
{
    try
    {
        cout << fixed << setprecision(3);
        const vector<size_t> distances = {8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128};
        // Один случайный порядок пар для всех расстояний обеспечивает сопоставимость.
        vector<size_t> order(PAIR_COUNT);
        iota(order.begin(), order.end(), 0);
        mt19937_64 generator(random_device{}());
        shuffle(order.begin(), order.end(), generator);

        cout << "===== Косвенное исследование размера строки кеша =====\n";
        cout << "Метод: зависимый обход пар узлов\n";
        cout << "Количество пар: " << PAIR_COUNT << '\n';
        cout << "Размер области пары: " << SLOT_SIZE << " байт\n";
        cout << "Выделено под области: " << PAIR_COUNT * SLOT_SIZE / 1024 << " КиБ\n";
        cout << "Размер узла: " << sizeof(Node) << " байт\n";
        cout << "Прогревочных проходов: " << WARM << '\n';
        cout << "Число измерений: " << TRIALS << '\n';
        cout << "Минимум обращений на измерение: " << MIN_ACCESSES << '\n';
        cout << "\nРасстояние, байт\tМедиана, нс\tСреднее, нс\tОтклонение, нс\tКоэфф. вариации, %\n";

        for (size_t distance : distances)
        {
            const Statistics stats = run_experiment(distance, order);
            cout << distance << "\t\t\t" << stats.median << "\t\t" << stats.mean << "\t\t" << stats.stddev << "\t\t" << stats.cv << '\n';
        }
        cout << "\nКонтрольный результат: " << (saved_result != 0 ? "ненулевой" : "нулевой") << '\n';
        cout << "Изменение времени может указывать на границу строки кеша, но не доказывает её размер.\n";
    }
    catch (const exception &e)
    {
        cerr << "Ошибка: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
