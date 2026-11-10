#include "hikari/application/motion_fit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace hikari::application {

namespace {

using Points = std::vector<std::pair<double, double>>;

std::optional<std::vector<double>> leastSquares(const Points &points, int degree)
{
    const auto size = static_cast<std::size_t>(degree + 1);
    if (points.size() < size)
        return std::nullopt;
    std::vector<std::vector<double>> a(size, std::vector<double>(size + 1, 0));
    for (const auto &[x, y] : points) {
        std::vector<double> powers(2 * size - 1, 1);
        for (std::size_t i = 1; i < powers.size(); ++i)
            powers[i] = powers[i - 1] * x;
        for (std::size_t r = 0; r < size; ++r) {
            for (std::size_t c = 0; c < size; ++c)
                a[r][c] += powers[r + c];
            a[r][size] += powers[r] * y;
        }
    }
    for (std::size_t col = 0; col < size; ++col) {
        const auto pivot = std::ranges::max_element(a.begin() + static_cast<std::ptrdiff_t>(col), a.end(),
                                                    {}, [&](const auto &row) { return std::abs(row[col]); });
        if (std::abs((*pivot)[col]) < 1e-12 * std::max(1.0, std::abs(a[0][0])))
            return std::nullopt;
        std::swap(a[col], *pivot);
        for (std::size_t r = 0; r < size; ++r) {
            if (r == col)
                continue;
            const double f = a[r][col] / a[col][col];
            for (std::size_t c = col; c <= size; ++c)
                a[r][c] -= f * a[col][c];
        }
    }
    std::vector<double> out(size);
    for (std::size_t i = 0; i < size; ++i)
        out[i] = a[i][size] / a[i][i];
    return out;
}

double evaluate(const std::vector<double> &coefficients, double x)
{
    double y = 0;
    for (std::size_t i = coefficients.size(); i-- > 0;)
        y = y * x + coefficients[i];
    return y;
}

double crossValidated(const Points &points, std::size_t k, int degree)
{
    const std::size_t n = points.size(), fold = n / k;
    double total = 0;
    for (std::size_t i = 0; i < k; ++i) {
        Points train, test;
        for (std::size_t j = 0; j < n; ++j)
            (j >= i * fold && j < (i + 1) * fold ? test : train).push_back(points[j]);
        const auto fit = leastSquares(train, degree);
        if (!fit || test.empty())
            return std::numeric_limits<double>::infinity();
        double squared = 0;
        for (const auto &[x, y] : test)
            squared += std::pow(y - evaluate(*fit, x), 2);
        total += std::sqrt(squared / static_cast<double>(test.size()));
    }
    return total / static_cast<double>(k);
}

}

double PolynomialFit::at(double x) const
{
    return evaluate(coefficients, x);
}

FitConfidence PolynomialFit::confidence() const
{
    return rmse > 5 ? FitConfidence::VeryLow : rmse > 0.2 ? FitConfidence::Low : FitConfidence::Good;
}

PolynomialFit fitPolynomial(std::span<const double> values)
{
    Points points;
    for (std::size_t i = 0; i < values.size(); ++i)
        points.emplace_back(static_cast<double>(i + 1), values[i]);
    PolynomialFit out;
    if (points.size() < 2) {
        out.coefficients = {values.empty() ? 0 : values.front()};
        return out;
    }
    const std::size_t k = std::max<std::size_t>(1, points.size() * 4 / 5);
    int degree = 1;
    double best = std::numeric_limits<double>::infinity();
    for (int d = 1; d <= 3; ++d) {
        const double rmse = crossValidated(points, k, d);
        if (rmse < best) {
            degree = d;
            best = rmse;
        }
        if (rmse < 0.01)
            break;
    }
    for (; degree >= 0; --degree)
        if (const auto fit = leastSquares(points, degree)) {
            out.coefficients = *fit;
            break;
        }
    out.rmse = std::isfinite(best) ? best : 0;
    return out;
}

Extension extendValues(std::span<const double> values, int frames, ExtendAt at)
{
    Extension out;
    std::vector<double> data(values.begin(), values.end());
    while (true) {
        out.fit = fitPolynomial(data);
        if (out.fit.rmse < 0.003 || data.size() * 2 < 15)
            break;
        const std::size_t n = data.size();
        std::vector<double> half;
        for (std::size_t i = 1; i <= n; ++i)
            if (at == ExtendAt::Start ? 2 * i < n : 2 * i > n)
                half.push_back(data[i - 1]);
        data = std::move(half);
    }
    if (data.empty())
        return out;
    if (at == ExtendAt::Start) {
        for (int x = 1 - frames; x <= 0; ++x)
            out.values.push_back(out.fit.at(x));
        out.values.push_back(data.front());
    } else {
        out.values.push_back(data.back());
        const auto n = static_cast<int>(data.size());
        for (int x = n + 1; x <= n + frames; ++x)
            out.values.push_back(out.fit.at(x));
    }
    return out;
}

}
