#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include "allocator_global_heap.h"
#include "pp_allocator.h"

int main() {
    try {
        // создаём глобальный аллокатор, который работает с глобальной кучей
        allocator_global_heap global_allocator;


        // создаём аллокатор для int и передаём ему наш global_allocator
        pp_allocator<int> int_alloc(&global_allocator);

        // создаём вектор int который будет использовать наш кастомный аллокатор
        std::vector<int, pp_allocator<int>> int_vec(int_alloc);

        // добавляем элементы в вектор
        int_vec.push_back(10);
        int_vec.push_back(20);

        // выводим содержимое вектора
        std::cout << "Integer vector content: ";
        for (int v : int_vec) {
            std::cout << v << " ";
        }
        std::cout << std::endl;

        // создаём аллокатор для строк
        pp_allocator<std::string> str_alloc(&global_allocator);

        // создаём вектор строк который использует тот же allocator
        std::vector<std::string, pp_allocator<std::string>> str_vec(str_alloc);

        // добавляем строки в вектор
        str_vec.push_back("Hello");
        str_vec.push_back("Global");
        str_vec.push_back("Heap");

        // выводим строки
        std::cout << "inside: ";
        for (const auto& s : str_vec) {
            std::cout << s << " ";
        }
        std::cout << std::endl;

        // объявляем свою структуру
        struct MyStruct {
            double d;
            char c;

            MyStruct(double d, char c) : d(d), c(c) {}
        };

        // создаём аллокатор для нашей структуры
        pp_allocator<MyStruct> struct_alloc(&global_allocator);

        // создаём объект структуры через аллокатор
        auto my_struct_ptr = struct_alloc.new_object<MyStruct>(3.14, 'A');

        // выводим содержимое структуры
        std::cout << "Custom struct content: d=" << my_struct_ptr->d << ", c=" << my_struct_ptr->c << std::endl;

        // удаляем объект через аллокатор
        struct_alloc.delete_object(my_struct_ptr);

    } catch (const std::exception& e) {
        // обработка стандартных исключений
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return 1;

    } catch (...) {
        // обработка любых других ошибок
        std::cerr << "Unknown exception caught" << std::endl;
        return 1;
    }

    // программа завершилась успешно
    return 0;
}