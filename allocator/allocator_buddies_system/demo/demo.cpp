#include <iostream>
#include <string>
#include <vector>
#include "../include/allocator_buddies_system.h"

int main() {
    std::cout << "Starting buddy allocator demonstration..." << std::endl;

    allocator_buddies_system buddy_alloc(4096, nullptr, allocator_with_fit_mode::fit_mode::first_fit);
    pp_allocator<int> int_alloc(&buddy_alloc);

    // Demonstrate allocating primitive types
    std::vector<int, pp_allocator<int>> vec(int_alloc);
    vec.push_back(10);
    vec.push_back(20);
    vec.push_back(30);

    std::cout << "Vector elements: ";
    for (int x : vec) std::cout << x << " ";
    std::cout << "\n";

    // Demonstrate allocating complex types
    pp_allocator<std::string> str_alloc(&buddy_alloc);
    std::string* str = str_alloc.allocate(1);
    str_alloc.construct(str, "Hello, Buddy System!");

    std::cout << "String element: " << *str << "\n";

    str_alloc.destroy(str);
    str_alloc.deallocate(str, 1);

    // Demonstrate array
    double* doubles = reinterpret_cast<double*>(buddy_alloc.allocate(sizeof(double) * 5));
    for (int i = 0; i < 5; ++i) doubles[i] = i * 1.5;

    std::cout << "Double array: ";
    for (int i = 0; i < 5; ++i) std::cout << doubles[i] << " ";
    std::cout << "\n";

    buddy_alloc.deallocate(doubles, sizeof(double) * 5);

    std::cout << "Demonstration finished successfully." << std::endl;
    return 0;
}
