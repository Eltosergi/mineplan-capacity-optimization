#pragma once
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// A scalar applies to the whole horizon; a list must specify every period.
inline std::vector<double> ParsePeriodValues(const std::string& text, int periods,
                                            const std::string& name, bool positive = false) {
    if (periods <= 0) throw std::invalid_argument("NumberOfPeriods must be positive");
    std::vector<double> values;
    std::size_t start = 0;
    do {
        const auto end = text.find(',', start);
        const auto token = text.substr(start, end == std::string::npos ? end : end - start);
        std::istringstream stream(token);
        double value;
        if (!(stream >> value) || !std::isfinite(value) || value < 0 || (positive && value == 0))
            throw std::invalid_argument(name + ": invalid value '" + token + "'");
        stream >> std::ws;
        if (!stream.eof()) throw std::invalid_argument(name + ": invalid value '" + token + "'");
        values.push_back(value);
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    if (values.size() == 1) values.resize(periods, values.front());
    if (values.size() != static_cast<std::size_t>(periods))
        throw std::invalid_argument(name + ": expected one value or " + std::to_string(periods) + " values");
    return values;
}
