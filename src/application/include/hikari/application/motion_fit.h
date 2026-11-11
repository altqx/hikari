#pragma once

#include <span>
#include <vector>

namespace hikari::application {

enum class FitConfidence { Good, Low, VeryLow };

// A least-squares polynomial through (1, values[0]) .. (n, values[n-1]); degree 1 to 3, the one with
// the lowest k-fold cross-validated RMSE (k = 80 % of the points), stopping at RMSE < 0.01.
struct PolynomialFit {
    std::vector<double> coefficients; // constant term first
    double rmse = 0;
    int degree() const { return static_cast<int>(coefficients.size()) - 1; }
    double at(double x) const;
    FitConfidence confidence() const;
};
PolynomialFit fitPolynomial(std::span<const double> values);

enum class ExtendAt { Start, End };
struct Extension {
    // Start: the new values, then the first given value. End: the last given value, then the new ones.
    std::vector<double> values;
    PolynomialFit fit;
};
// While the fit's RMSE is 0.003 or more and at least 8 values remain, it keeps only the half nearer
// the extended end and fits again.
Extension extendValues(std::span<const double> values, int frames, ExtendAt at);

}
