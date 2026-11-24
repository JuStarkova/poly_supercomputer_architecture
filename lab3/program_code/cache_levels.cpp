#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

const size_t MIN_WORKING_SET = 1024;                 // минимальный размер рабочего набора -- 1 килобайт
const size_t MAX_WORKING_SET = 128ULL * 1024 * 1024; // максимум 128 мегабайт
const size_t MIN_MEASURE_ACCESSES = 5'000'000;       // минимальное число переходов по указателям чтобы можно было достаточно точно определить время обращения
int WARM = 4;                                        // число прогревочных
int TRIALS = 15;                                     // число испытаний

// структура одного узла указывающий на след. узел
struct alignas(64) Element // выравнивание 64 байта
{
    Element *next = nullptr;
};

// Сохраняем результат, чтобы компилятор не удалил обход цепочки.
volatile uintptr_t saved_result = 0;

// для отображения размеров памяти
string format_size(size_t bytes)
{
    ostringstream out;
    out << fixed << setprecision(2) << (bytes < 1024 * 1024 ? bytes / 1024.0 : bytes / (1024.0 * 1024.0)); // обязательно 2 цифры после запятой
    return out.str() + (bytes < 1024 * 1024 ? " КиБ" : " МиБ");
}

// случайная цепочка указателей
Element *build_random_cycle(vector<Element> &elements, mt19937_64 &generator)
{
    vector<uint32_t> order(elements.size()); // порядок обхода элементов цепочки, беззнаковый инт 32 бита
    for (size_t i = 0; i < order.size(); i++)
        order[i] = i;
    shuffle(order.begin(), order.end(), generator);

    for (size_t i = 0; i < order.size(); i++)
    {
        size_t current = order[i];
        size_t next = order[(i + 1) % order.size()]; // % для замыкания в цикл
        elements[current].next = &elements[next];    // сохраняем ссылку следующего элемента в поле текущего элемента
    }

    return &elements[order[0]]; // адрес первого элемента
}

// steps переходов по указателям
Element *pointer_chase(Element *current, size_t steps)
{
    for (size_t i = 0; i < steps; ++i)
        current = current->next;
    return current;
}

struct Result
{
    size_t working_set_bytes; // размер рабочего набора
    double median;
    double minimum;
    double maximum;
    vector<double> times;
};

// запуск одного эксперимента
Result run_experiment(size_t working_set_bytes, mt19937_64 &generator, size_t trials, size_t min_steps)
{
    using Clock = chrono::steady_clock;
    const size_t n = working_set_bytes / sizeof(Element);     // число элементов
    vector<Element> elements(n);                              // вектор n элементов
    Element *start = build_random_cycle(elements, generator); // получили цикл и значение первого элемента, т.е. указатель на второй элемент

    const size_t cycles = ceil(static_cast<double>(min_steps) / n); // число полных циклов с округлением вверх, учитывается желаемое число переходов

    const size_t steps = n * cycles;
    vector<double> times; // для результатов вычислений

    cout << "Измерение для " << format_size(working_set_bytes) << endl;
    for (size_t trial = 0; trial < trials; ++trial)
    {
        for (int warm = 0; warm < WARM; ++warm)
            saved_result = reinterpret_cast<uintptr_t>(pointer_chase(start, n)); // один цикл по цепочке

        const auto time_start = Clock::now();
        Element *result = pointer_chase(start, steps); // проходимся по циклу steps раз
        const auto time_end = Clock::now();
        saved_result = reinterpret_cast<uintptr_t>(result); // чтобы не оптимизировались вычисления

        const double elapsed_ns = chrono::duration<double, nano>(time_end - time_start).count(); // время от начала до конца прохождения всех циклов
        times.push_back(elapsed_ns / steps);                                                     // среднее время на один переход в наносекундах
        cout << "Испытание " << trial + 1 << ": " << times.back() << " нс/шаг\n";
    }

    vector<double> sorted_times = times;
    sort(sorted_times.begin(), sorted_times.end());
    const size_t middle = sorted_times.size() / 2;
    const double median = sorted_times.size() % 2 == 0 ? (sorted_times[middle - 1] + sorted_times[middle]) / 2.0 : sorted_times[middle];
    // возвращается структура result
    return {working_set_bytes, median, sorted_times.front(), sorted_times.back(), times};
}

int main()
{
    vector<size_t> working_sizes;
    for (size_t size = MIN_WORKING_SET; size <= MAX_WORKING_SET; size *= 2)
    {
        working_sizes.push_back(size);
        if (size < MAX_WORKING_SET)
        {
            working_sizes.push_back(size + size / 4);
            working_sizes.push_back(size + size / 2);
        }
    }

    cout << fixed << setprecision(2);
    cout << "Испытаний: " << TRIALS << "; минимум шагов: " << MIN_MEASURE_ACCESSES << "; прогревов: " << WARM << ".\n";

    mt19937_64 generator(random_device{}());
    shuffle(working_sizes.begin(), working_sizes.end(), generator);

    vector<Result> results;
    for (size_t size : working_sizes)
        results.push_back(run_experiment(size, generator, TRIALS, MIN_MEASURE_ACCESSES));

    // сортировка размеров памяти
    for (size_t i = 0; i < results.size(); i++)
    {
        for (size_t j = i + 1; j < results.size(); j++)
        {
            if (results[j].working_set_bytes < results[i].working_set_bytes)
                swap(results[i], results[j]);
        }
    }

    // вывод результатов
    cout << fixed << setprecision(2);
    cout << "\n===== ВСЕ ИЗМЕРЕНИЯ (нс/шаг) =====\n";
    cout << "Рабочий набор";
    for (int i = 0; i < TRIALS; ++i)
        cout << "\t№" << i + 1;
    cout << "\tМедиана\n";
    for (const Result &result : results)
    {
        cout << format_size(result.working_set_bytes);
        for (double time : result.times)
            cout << "\t" << time;
        cout << "\t" << result.median << '\n';
    }
    cout << "\n===== РЕЗУЛЬТАТЫ =====\n";
    cout << "Рабочий набор\tМедиана\tМин-Макс (нс/шаг)\n";
    for (const Result &result : results)
        cout << format_size(result.working_set_bytes) << "\t" << result.median << "\t" << result.minimum << "-" << result.maximum << '\n';

    return 0;
}
