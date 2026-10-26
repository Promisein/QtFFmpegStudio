#include "mux_progress.h"

#include <iostream>

int main()
{
    MuxProgress both(10.0, true);
    if (both.observe(true, 2.0).has_value()) return 1;
    if (both.observe(false, 1.0) != 10) return 2;
    if (both.observe(false, 3.0) != 20) return 3;
    if (both.observe(true, 5.0) != 30) return 4;
    if (both.observe(true, 4.0).has_value()) return 5;
    if (both.observe(false, 12.0) != 50) return 6;
    if (both.observe(true, 20.0) != 99) return 7;
    if (both.observe(false, 20.0).has_value()) return 8;

    MuxProgress videoOnly(4.0, false);
    if (videoOnly.observe(true, 1.0) != 25) return 9;
    if (videoOnly.observe(true, 4.0) != 99) return 10;
    MuxProgress unknown(0.0, true);
    if (unknown.observe(true, 2.0).has_value()
        || unknown.observe(false, 2.0).has_value()) return 11;
    std::cout << "mux progress PASS\n";
    return 0;
}
