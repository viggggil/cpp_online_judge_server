#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "usage: checker <expected> <actual>\n";
        return 1;
    }

    std::ifstream expected(argv[1]);
    std::ifstream actual(argv[2]);
    std::string expected_text;
    std::string actual_text;

    std::getline(expected, expected_text, '\0');
    std::getline(actual, actual_text, '\0');

    if (expected_text == actual_text) {
        return 0;
    }

    std::cerr << "output mismatch\n";
    return 2;
}