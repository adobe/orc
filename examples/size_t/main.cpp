// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

#include <iostream>
#include <string>

std::string one();
std::string two();

int main() {
    std::cout << "one: " << one() << '\n';
    std::cout << "two: " << two() << '\n';
}
