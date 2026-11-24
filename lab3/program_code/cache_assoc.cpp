#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>
#include <unistd.h>

using namespace std;

constexpr size_t CACHE_LINE_SIZE = 64; // 64 байта
constexpr size_t ADDRESS_STRIDE = 4096;
constexpr size_t MAX_LINES = 16;
constexpr size_t WARM = 4;
constexpr size_t TRIALS = 15;
constexpr size_t MIN_ACCESSES = 5'000'000;

struct alignas(64) Node
{
    Node *next;
};

volatile uintptr_t result_sink = 0;

Node *pointer_chase(Node *current, size_t steps) noexcept
{
    for (size_t i = 0; i < steps; ++i)
    {
        current = current->next;
    }
    return current;
}

bool validate_cycle(const vector<Node *> &nodes, const vector<size_t> &order, Node *start)
{
    Node *current = start;
    for (size_t i = 0; i < order.size(); ++i)
    {
        if (current != nodes[order[i]])
        {
            return false;
        }
        current = current->next;
    }
    return current == start;
}

// среднее время одного перехода по указателю 
double measure(Node *start, size_t node_count)
{
    using Clock = chrono::steady_clock;
    const size_t passes = (MIN_ACCESSES + node_count - 1) / node_count;
    const size_t measured_steps = passes * node_count;
    vector<double> measurements;
    measurements.reserve(TRIALS);

    for (size_t trial = 0; trial < TRIALS; ++trial)
    {
        for (size_t warm = 0; warm < WARM; ++warm)
        {
            Node *result = pointer_chase(start, node_count);
            result_sink = reinterpret_cast<uintptr_t>(result);
            if (result != start)
            {
                throw runtime_error("Ошибка проверки при прогреве");
            }
        }

        const auto time_start = Clock::now();
        Node *result = pointer_chase(start, measured_steps);
        const auto time_end = Clock::now();
        result_sink = reinterpret_cast<uintptr_t>(result);
        if (result != start)
        {
            throw runtime_error("Ошибка проверки при измерении");
        }
        const double elapsed_ns = chrono::duration<double, nano>(time_end - time_start).count();
        measurements.push_back(elapsed_ns / measured_steps);
    }
    sort(measurements.begin(), measurements.end());
    return measurements[measurements.size() / 2];
}

double run_experiment(const vector<size_t> &order, size_t page_size)
{
    const size_t line_count = order.size();
    const size_t allocation_size = line_count * page_size;
    void *raw_memory = nullptr;
    if (posix_memalign(&raw_memory, page_size, allocation_size) != 0)
    {
        throw runtime_error("Не удалось выделить память");
    }
    unique_ptr<void, decltype(&free)> memory(raw_memory, &free);
    auto *base = static_cast<unsigned char *>(memory.get());
    vector<Node *> nodes(line_count);

    // Узлы в начале страниц при типичной индексации L1 попадают в один набор.
    for (size_t i = 0; i < line_count; ++i)
    {
        unsigned char *address = base + i * page_size;
        nodes[i] = ::new (static_cast<void *>(address)) Node{};
    }

    for (size_t i = 0; i < line_count; ++i)
    {
        nodes[order[i]]->next = nodes[order[(i + 1) % line_count]];
    }
    Node *start = nodes[order[0]];
    if (!validate_cycle(nodes, order, start))
    {
        throw runtime_error("Ошибка проверки цепочки");
    }
    return measure(start, line_count);
}

int main()
{
    try
    {
        cout << fixed << setprecision(3);
        const long system_page_size = sysconf(_SC_PAGESIZE);
        if (system_page_size <= 0)
        {
            throw runtime_error("Не удалось определить размер страницы");
        }
        const size_t page_size = static_cast<size_t>(system_page_size);
        if (page_size < ADDRESS_STRIDE || page_size % ADDRESS_STRIDE != 0 || (page_size & (page_size - 1)) != 0)
        {
            throw runtime_error("Неподдерживаемый размер страницы");
        }

        for (size_t count = 1; count <= MAX_LINES; ++count)
        {
            vector<size_t> order(count);
            iota(order.begin(), order.end(), 0);
            mt19937_64 generator(count);
            shuffle(order.begin(), order.end(), generator);
            const double median = run_experiment(order, page_size);
            cout << setw(4) << count << setw(12) << median << '\n';
        }
    }
    catch (const exception &e)
    {
        cerr << "Ошибка: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
