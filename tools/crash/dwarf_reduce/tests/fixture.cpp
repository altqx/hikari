#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace outer {
struct Table {
    std::map<std::string, std::vector<int>> rows;
    int count(const std::string &key) const;
    template <typename F> int each(F &&f) const
    {
        int total = 0;
        for (const auto &[key, row] : rows)
            for (int value : row)
                total += f(key, value);
        return total;
    }
};

int Table::count(const std::string &key) const
{
    auto it = rows.find(key);
    return it == rows.end() ? 0 : int(it->second.size());
}
}

static int afterAFunctionWithParameters(int a, int b)
{
    return a * b + std::printf("%d\n", a);
}

namespace {
namespace inner {
__attribute__((noinline)) int deep(const outer::Table &table, const std::string &key)
{
    std::function<int(const std::string &, int)> weigh = [&](const std::string &k, int v) {
        return k == key ? v * 2 : v;
    };
    int sum = 0;
    {
        std::vector<int> local(table.rows.size(), 1);
        for (int v : local)
            sum += v + table.count(key);
    }
    return sum + table.each(weigh);
}
}
}

int main(int argc, char **argv)
{
    outer::Table table;
    table.rows[argc > 1 ? argv[1] : "x"] = {1, 2, 3};
    return inner::deep(table, "x") + afterAFunctionWithParameters(argc, 3);
}
