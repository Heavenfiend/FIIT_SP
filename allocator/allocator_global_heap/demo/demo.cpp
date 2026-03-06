#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include "allocator_global_heap.h"
#include "pp_allocator.h"

int main() {
    try {
        allocator_global_heap global_allocator;

        // Demonstrate allocating integers
        pp_allocator<int> int_alloc(&global_allocator);
        std::vector<int, pp_allocator<int>> int_vec(int_alloc);
        int_vec.push_back(10);
        int_vec.push_back(20);

        std::cout << "Integer vector content: ";
        for (int v : int_vec) {
            std::cout << v << " ";
        }
        std::cout << std::endl;

        // Demonstrate allocating strings
        pp_allocator<std::string> str_alloc(&global_allocator);
        std::vector<std::string, pp_allocator<std::string>> str_vec(str_alloc);
        str_vec.push_back("Hello");
        str_vec.push_back("Global");
        str_vec.push_back("Heap");

        std::cout << "String vector content: ";
        for (const auto& s : str_vec) {
            std::cout << s << " ";
        }
        std::cout << std::endl;

        // Demonstrate allocating custom types
        struct MyStruct {
            double d;
            char c;
            MyStruct(double d, char c) : d(d), c(c) {}
        };

        pp_allocator<MyStruct> struct_alloc(&global_allocator);
        auto my_struct_ptr = struct_alloc.new_object<MyStruct>(3.14, 'A');
        std::cout << "Custom struct content: d=" << my_struct_ptr->d << ", c=" << my_struct_ptr->c << std::endl;
        struct_alloc.delete_object(my_struct_ptr);

    } catch (const std::exception& e) {
        std::cerr << "Exception caught: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception caught" << std::endl;
        return 1;
    }

    return 0;
}
