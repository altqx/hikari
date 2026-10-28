// Runs legacy time code on fixture values and prints one observation per line.
//   cmp <lhs_ms> <rhs_ms>                -> ge le eq gt lt   (SubsTime operators)
//   offset <anchor_ms> <target_ms> <start_ms>...
//                                        -> legacy SubsGridBase.cpp:534 composition:
//                                           Timebase::FrameAt(target - anchor)
#include "SubsTime.h"
#include "Timebase.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op;
        in >> op;
        if (op == "cmp") {
            int l, r;
            in >> l >> r;
            SubsTime a(l), b(r);
            std::printf("cmp %d %d ge=%d le=%d eq=%d gt=%d lt=%d\n", l, r,
                a >= b, a <= b, a == b, a > b, a < b);
        } else if (op == "offset") {
            int anchor, target, s;
            in >> anchor >> target;
            std::vector<int> starts;
            while (in >> s)
                starts.push_back(s);
            Timebase tb = Timebase::FromTimecodes(starts, 0.f);
            std::printf("offset %d %d legacy=%d\n", anchor, target, tb.FrameAt(target - anchor));
        }
    }
    return 0;
}
