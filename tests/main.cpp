#include "check.hpp"

#include <cstdio>
#include <exception>

int main() {
    int failed = 0;
    for (const check::Case& c : check::cases()) {
        try {
            c.run();
            std::printf("[ ok ] %s\n", c.name);
        } catch (const check::Failure& f) {
            ++failed;
            std::printf("[FAIL] %s\n       %s\n", c.name, f.message.c_str());
        } catch (const std::exception& e) {
            ++failed;
            std::printf("[FAIL] %s\n       unexpected exception: %s\n", c.name, e.what());
        }
    }
    std::printf("%zu tests, %d failed\n", check::cases().size(), failed);
    return failed == 0 ? 0 : 1;
}
