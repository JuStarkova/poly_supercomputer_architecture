#include <iostream>
#include <string>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace std;

// функция перевода целой части числа из двоичной системы в десятичную
string binaryToIntPart(string binaryNumber)
{
    int decimalResult = 0;
    if (binaryNumber == "0")
    {
        return "0";
    }
    for (int i = binaryNumber.length() - 1; i >= 0; i--)
    {
        if (binaryNumber[i] == '1')
        {
            decimalResult += pow(2, (binaryNumber.length() - 1 - i));
        }
    }
    return to_string(decimalResult);
}

// функция перевода целой части числа из десятичной системы в двоичную
string intPartToBinary(int decimalNumber)
{
    if (decimalNumber == 0)
    {
        return "0";
    }

    string binaryResult = "";
    while (decimalNumber >= 1)
    {
        binaryResult += (decimalNumber % 2 == 0) ? '0' : '1';
        decimalNumber /= 2;
    }

    reverse(binaryResult.begin(), binaryResult.end());
    return binaryResult;
}

// функция перевода дробной части числа из десятичной системы в двоичную
// (всегда даёт 10 бит -- под bf16 из них потом берутся только первые 7)
string fractionToBinary(double fraction)
{
    string result = "";
    for (int i = 0; i < 10; i++)
    {
        fraction *= 2;
        if (fraction >= 1)
        {
            result += '1';
            fraction -= 1;
        }
        else
        {
            result += '0';
        }
    }
    return result;
}

// дополнение строки нулями слева до нужной длины
string padLeftZeros(string s, size_t width)
{
    while (s.length() < width)
        s = "0" + s;
    return s;
}

string decimalTo_bf16(double decimalNumber)
{
    string result = "";

    if (decimalNumber < 0)
        result += '1';
    else
        result += '0';

    double absVal = fabs(decimalNumber); // модуль числа

    if (absVal == 0.0)
    {
        result += "00000000"; // 8 бит экспоненты
        result += "0000000";  // 7 бит мантиссы
        return result;
    }

    // поиск несмещённой экспоненты
    int exponent;
    if (absVal >= 1.0)
    {
        int wholePart = (int)absVal;
        string wholePartBinary = intPartToBinary(wholePart);
        exponent = (int)wholePartBinary.length() - 1;
    }
    else
    {
        // обеспечиваем ведущую единицу перед запятой (умножаем на 2 число)
        exponent = 0;
        double copy_absVal = absVal;
        while (copy_absVal < 1.0)
        {
            copy_absVal *= 2;
            exponent--;
        }
    }

    // переполнение: поле экспоненты не должно доходить до 255 (резерв под inf/NaN)
    if (exponent + 127 >= 255)
    {
        cout << "Внимание: число слишком велико для bf16, "
                "будет сохранено как условная бесконечность.\n";
        result += "11111111";
        result += "0000000";
        return result;
    }

    // денормализованное число: несмещённая экспонента меньше -126
    if (exponent < -126)
    {
        double scaled = absVal / pow(2, -126); // приводим к виду 0.mantissa
        string mantissaBits = fractionToBinary(scaled).substr(0, 7);
        result += "00000000";   // экспонента денормализованного числа всегда 0
        result += mantissaBits; // неявная единица не прибавляется
        return result;
    }

    // нормализованное число
    string mantissaBits;
    if (absVal >= 1.0)
    {
        int wholePart = (int)absVal;
        string wholePartBinary = intPartToBinary(wholePart);
        double fractionPart = absVal - wholePart;
        string fractionPartBinary = fractionToBinary(fractionPart);
        string combined = wholePartBinary.substr(1) + fractionPartBinary;
        mantissaBits = combined.substr(0, 7); // берём только 7 бит под bf16
    }
    else
    {
        double normalizedFraction = absVal / pow(2, exponent) - 1.0;
        mantissaBits = fractionToBinary(normalizedFraction).substr(0, 7);
    }

    string exponentBinary = padLeftZeros(intPartToBinary(exponent + 127), 8);

    result += exponentBinary;
    result += mantissaBits;
    return result;
}

double binaryBF16ToDecimal(string bits)
{
    char signBit = bits[0];
    string exponentBits = bits.substr(1, 8); // 8 бит экспоненты
    string mantissaBits = bits.substr(9, 7); // 7 бит мантиссы

    int E = stoi(binaryToIntPart(exponentBits));
    int M = stoi(binaryToIntPart(mantissaBits));

    double value;

    if (E == 0 && M == 0)
    {
        value = 0.0;
    }
    else if (E == 0 && M != 0)
    {
        // денормализованное число: единицу не прибавляем
        value = (M / 128.0) * pow(2, -126);
    }
    else if (E == 255)
    {
        value = (M == 0) ? INFINITY : NAN;
    }
    else
    {
        int unbiasedExponent = E - 127;
        value = ((M / 128.0) + 1) * pow(2, unbiasedExponent);
    }

    if (signBit == '1')
        value = -value;

    return value;
}

bool isValidBinaryBF16(const string &s)
{
    if (s.length() != 16)
        return false; // bf16 тоже занимает 16 бит
    for (char c : s)
        if (c != '0' && c != '1')
            return false;
    return true;
}

double readDouble(const string &prompt)
{
    double value;
    while (true)
    {
        cout << prompt;
        if (cin >> value)
            return value;
        cout << "Ошибка: введите корректное десятичное число (например -3.5).\n";
        cin.clear();
        cin.ignore(numeric_limits<streamsize>::max(), '\n');
    }
}

string readBinaryBF16(const string &prompt)
{
    string s;
    while (true)
    {
        cout << prompt;
        if (!(cin >> s))
        {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }
        if (isValidBinaryBF16(s))
            return s;
        cout << "Ошибка: нужно ввести ровно 16 символов, состоящих только из 0 и 1.\n";
    }
}

void printBF16(const string &bits)
{
    cout << bits[0] << " " << bits.substr(1, 8) << " " << bits.substr(9, 7)
         << "   (слитно: " << bits << ")\n";
}

void showMenu()
{
    cout << "\nПеревод чисел по стандарту bf16\n";
    cout << "1 - Перевести десятичное число в двоичный код\n";
    cout << "2 - Перевести 16-битный код в десятичное число\n";
    cout << "0 - Выход\n";
    cout << "Выберите пункт меню: ";
}

int main()
{
    while (true)
    {
        showMenu();
        int choice;
        if (!(cin >> choice))
        {
            cout << "Ошибка\n";
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            continue;
        }

        if (choice == 0)
        {
            cout << "Завершение работы программы.\n";
            break;
        }
        else if (choice == 1)
        {
            double number = readDouble("Введите десятичное число: ");
            string bits = decimalTo_bf16(number);
            cout << "Результат: ";
            printBF16(bits);
        }
        else if (choice == 2)
        {
            string bits = readBinaryBF16(
                "Введите 16 бит (пример 0011111110000000): ");
            double value = binaryBF16ToDecimal(bits);
            cout << "Десятичное значение: " << value << "\n";
        }
        else
        {
            cout << "Нет такого пункта меню, попробуйте снова.\n";
        }
    }
    return 0;
}
