#include "utils.h"

std::string cfs::utils::getenv(const std::string& name)
{
    const auto var = ::getenv(name.c_str());
    if (var == nullptr) {
        return "";
    }

    return var;
}

std::string cfs::utils::replace_all(
    std::string & original,
    const std::string & target,
    const std::string & replacement)
{
    if (target.empty()) return original; // Avoid infinite loop if target is empty

    if (target.size() == 1 && replacement.empty()) {
        std::erase_if(original, [&target](const char c) { return c == target[0]; });
        return original;
    }

    size_t pos = 0;
    while ((pos = original.find(target, pos)) != std::string::npos) {
        original.replace(pos, target.length(), replacement);
        pos += replacement.length(); // Move past the replacement to avoid infinite loop
    }

    return original;
}
