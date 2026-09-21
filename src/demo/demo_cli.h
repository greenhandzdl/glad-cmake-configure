#ifndef DEMO_CLI_H
#define DEMO_CLI_H

/**
 * @file demo_cli.h
 * @brief Command-line switches shared by the demo executables under src/demo/.
 *
 * Deliberately *not* part of module gfx: this is application-side test
 * plumbing. Its reason for existing is that a feature can then be forced on or
 * off from a script instead of from the keyboard, which makes the
 * "toggle everything, diff the frame" sweep automatable:
 *
 *   ./output/voxel_terrain --off fog,water --quit-after 11
 *   ./output/pbr_showcase  --on debug --quit-after 6
 *
 * `--quit-after` is what lets such a run end on its own, and `--help` prints
 * the names the executable actually understands, so a typo in a regression
 * script is reported rather than silently ignored.
 */

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace demo {

class Flags {
public:
    // `features` are the names --off/--on accept for this executable,
    // `options` the names that take a number (--auto-break 20).
    Flags(int argc, char** argv, std::initializer_list<std::string_view> features,
          std::initializer_list<std::string_view> options, std::string_view program)
        : program_(program) {
        for (const std::string_view f : features) features_.emplace_back(f);
        for (const std::string_view o : options) options_.emplace_back(o);

        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--help" || arg == "-h") {
                help_ = true;
                continue;
            }
            if (arg == "--off" || arg == "--on") {
                const bool off = arg == "--off";
                const char* list = ValueAfter(argc, argv, i, arg);
                if (list) ParseFeatureList(list, off);
                continue;
            }
            if (arg == "--quit-after") {
                const char* text = ValueAfter(argc, argv, i, arg);
                double seconds = 0.0;
                if (text && ParseNumber(text, seconds) && seconds > 0.0) quitAfter_ = seconds;
                continue;
            }
            if (arg.size() > 2 && arg[1] == '-' && arg[2] != '-') {
                const std::size_t eq = arg.find('=');
                const std::string_view name = arg.substr(2, eq == std::string_view::npos
                                                               ? std::string_view::npos
                                                               : eq - 2);
                if (!contains(options_, name)) {
                    std::fprintf(stderr, "%s: unknown argument '%s' (--help lists the switches)\n",
                                 program_.c_str(), std::string(arg).c_str());
                    continue;
                }
                const char* text = eq == std::string_view::npos
                                       ? ValueAfter(argc, argv, i, arg)
                                       : argv[i] + eq + 1;
                // An option value is a number when it parses as one and a plain
                // string otherwise (e.g. --model path/to/thing.obj); the accessor
                // chosen at read time decides how it is consumed.
                if (text) {
                    double value = 0.0;
                    if (TryNumber(text, value)) {
                        values_.emplace_back(std::string(name), value);
                    } else {
                        strValues_.emplace_back(std::string(name), std::string(text));
                    }
                }
                continue;
            }
            std::fprintf(stderr, "%s: unknown argument '%s' (--help lists the switches)\n",
                         program_.c_str(), std::string(arg).c_str());
        }
    }

    Flags(const Flags&)            = delete;
    Flags& operator=(const Flags&) = delete;

    // The state a feature starts in: --on/--off win over the built-in default,
    // and --on wins over --off so a script can flip one name out of a list.
    [[nodiscard]] bool on(std::string_view name, bool defaultOn = true) const {
        if (contains(on_, name)) return true;
        if (contains(off_, name)) return false;
        return defaultOn;
    }

    // The numeric option `name`, or `fallback` when it was not given.
    [[nodiscard]] double number(std::string_view name, double fallback = 0.0) const {
        for (const auto& [key, value] : values_)
            if (key == name) return value;
        return fallback;
    }

    // The same option read as a count or an index. argv is user input and
    // strtod() cheerfully accepts "inf", "nan" and "1e300", while casting one
    // of those to int is undefined behaviour - so the range test happens while
    // the value is still a double and anything outside it saturates. A NaN lands
    // on the lower bound: a NaN count means “no scripted edits”, which is also
    // what an absent one means.
    [[nodiscard]] int integer(std::string_view name, int fallback = 0) const {
        constexpr double kMin = static_cast<double>(std::numeric_limits<int>::min());
        constexpr double kMax = static_cast<double>(std::numeric_limits<int>::max()) - 1.0;
        const double value = number(name, static_cast<double>(fallback));
        if (!(value > kMin)) return std::numeric_limits<int>::min();
        if (value > kMax) return std::numeric_limits<int>::max();
        return static_cast<int>(value);   // in range now, truncation intended
    }

    // The same option read as an angle or a distance. The standard leaves a
    // double that float cannot represent undefined at the conversion, and on the
    // toolchains tried here not even UBSan reports it - so the bound is applied
    // while the value is still a double, and a NaN or infinity falls back to the
    // default instead of silently poisoning the camera.
    [[nodiscard]] float real(std::string_view name, float fallback = 0.0f,
                              float lo = -3.0e38f, float hi = 3.0e38f) const {
        double value = number(name, static_cast<double>(fallback));
        if (!std::isfinite(value)) return fallback;
        if (value < static_cast<double>(lo)) value = lo;
        if (value > static_cast<double>(hi)) value = hi;
        return static_cast<float>(value);
    }

    // The same option read verbatim as a string (a path, a mode name...). An
    // option that parsed as a number is not visible here, so a demo declares a
    // given name as either numeric or textual, never both.
    [[nodiscard]] std::string string(std::string_view name, std::string_view fallback = "") const {
        for (const auto& [key, value] : strValues_)
            if (key == name) return value;
        return std::string(fallback);
    }

    // Seconds before the window closes itself; 0 means "until the user does".
    [[nodiscard]] double quitAfter() const noexcept { return quitAfter_; }
    [[nodiscard]] bool wantsHelp() const noexcept { return help_; }

    void printUsage() const {
        std::printf("usage: %s [--off a,b] [--on a,b] [--quit-after SECONDS] [--help]\n",
                    program_.c_str());
        std::printf("  features: ");
        for (std::size_t i = 0; i < features_.size(); ++i)
            std::printf("%s%s", i ? "," : "", features_[i].c_str());
        std::printf("\n  options:  ");
        for (std::size_t i = 0; i < options_.size(); ++i)
            std::printf("%s--%s N", i ? " " : "", options_[i].c_str());
        std::printf("\n");
    }

private:
    static bool contains(const std::vector<std::string>& list, std::string_view name) {
        for (const std::string& item : list)
            if (item == name) return true;
        return false;
    }

    bool ParseNumber(const char* text, double& out) {
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        if (end == text || !end || *end != '\0') {
            std::fprintf(stderr, "%s: wants a number, got '%s'\n",
                         program_.c_str(), text);
            return false;
        }
        out = value;
        return true;
    }

    // Silent sibling of ParseNumber: a non-numeric option value is not an error,
    // it just means the caller wanted a string, so no message is printed here.
    static bool TryNumber(const char* text, double& out) {
        char* end = nullptr;
        const double value = std::strtod(text, &end);
        if (end == text || !end || *end != '\0') return false;
        out = value;
        return true;
    }

    // The value of "--name VALUE", which also consumes the following argv slot.
    const char* ValueAfter(int argc, char** argv, int& index, std::string_view name) {
        if (index + 1 >= argc) {
            std::fprintf(stderr, "%s: %s needs a value\n",
                         program_.c_str(), std::string(name).c_str());
            return nullptr;
        }
        return argv[++index];
    }

    void ParseFeatureList(const char* text, bool off) {
        std::string_view rest(text);
        while (!rest.empty()) {
            const std::size_t comma = rest.find(',');
            const std::string_view name = rest.substr(0, comma);
            rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
            if (name.empty()) continue;
            if (!contains(features_, name)) {
                std::fprintf(stderr, "%s: unknown feature '%s' (--help lists the valid ones)\n",
                             program_.c_str(), std::string(name).c_str());
                continue;
            }
            (off ? off_ : on_).emplace_back(name);
        }
    }

    std::vector<std::string> features_;
    std::vector<std::string> options_;
    std::vector<std::pair<std::string, double>> values_;
    std::vector<std::pair<std::string, std::string>> strValues_;
    std::vector<std::string> off_;
    std::vector<std::string> on_;
    std::string program_;
    double quitAfter_ = 0.0;
    bool help_ = false;
};

} // namespace demo

#endif // DEMO_CLI_H
