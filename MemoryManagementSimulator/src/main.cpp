#include <iostream>

#include "CliApp.h"

int main() {
    memsim::CliApp app(std::cin, std::cout);
    return app.run();
}
