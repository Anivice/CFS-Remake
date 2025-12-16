#ifndef CFS_LOGGER_H
#define CFS_LOGGER_H

#include <iostream>
#include <mutex>
#include <map>
#include <unordered_map>
#include <atomic>
#include <iomanip>
#include <ranges>
#include <vector>
#include <tuple>      // for std::tuple, std::make_tuple
#include <utility>    // for std::forward
#include <any>
#include <cstring>
#include <regex>
#include <chrono>
#include <functional>
#include <cstddef>
#include <type_traits>
#include <source_location>
#include <algorithm>
#include <sys/stat.h>
#include <fstream>
#include "colors.h"

#define construct_simple_type_compare(type)                             \
    template <typename T>                                               \
    struct is_##type : std::false_type {};                              \
    template <>                                                         \
    struct is_##type<type> : std::true_type { };                        \
    template <typename T>                                               \
    constexpr bool is_##type##_v = is_##type<T>::value;

namespace cfs::log
{
    template <typename T, typename = void> struct is_container : std::false_type { };
    template <typename T>
    struct is_container<T,
        std::void_t<decltype(std::begin(std::declval<T>())),
        decltype(std::end(std::declval<T>()))>> : std::true_type { };
    template <typename T> constexpr bool is_container_v = is_container<T>::value;

    template <typename T> struct is_map : std::false_type { };
    template <typename Key, typename Value> struct is_map<std::map<Key, Value>> : std::true_type { };
    template <typename T> constexpr bool is_map_v = is_map<T>::value;

    template <typename T> struct is_unordered_map : std::false_type {};
    template <typename Key, typename Value, typename Hash, typename KeyEqual, typename Allocator>
    struct is_unordered_map< std::unordered_map<Key, Value, Hash, KeyEqual, Allocator> > : std::true_type {};
    template <typename T> constexpr bool is_unordered_map_v = is_unordered_map<T>::value;

    template <typename T> struct is_string : std::false_type { };
    template <> struct is_string<std::string> : std::true_type { };
    template <> struct is_string<const char*> : std::true_type { };
    template <> struct is_string<std::string_view> : std::true_type { };
    template <typename T> constexpr bool is_string_v = is_string<T>::value;

    construct_simple_type_compare(bool);

    inline class debug_log_t {} debug_log;
    construct_simple_type_compare(debug_log_t);

    inline class info_log_t {} info_log;
    construct_simple_type_compare(info_log_t);

    inline class warning_log_t {} warning_log;
    construct_simple_type_compare(warning_log_t);

    inline class error_log_t {} error_log;
    construct_simple_type_compare(error_log_t);

    inline class cursor_off_t {} cursor_off;
    construct_simple_type_compare(cursor_off_t);

    inline class cursor_on_t {} cursor_on;
    construct_simple_type_compare(cursor_on_t);

    template <typename T> struct is_pair : std::false_type { };
    template <typename Key, typename Value> struct is_pair<std::pair<Key, Value>> : std::true_type { };
    template <typename T> constexpr bool is_pair_v = is_pair<T>::value;

    template <typename T, typename Tag> struct strong_typedef
    {
        T value;
        strong_typedef() = default;
        explicit strong_typedef(T v) : value(std::move(v)) {}
        T& get() noexcept { return value; }
        [[nodiscard]] const T& get() const noexcept { return value; }
        explicit operator T&() noexcept { return value; }
        explicit operator const T&() const noexcept { return value; }
    };

    struct prefix_string_tag {};
    using prefix_string_t = strong_typedef<std::string, prefix_string_tag>;

    template < typename StringType >
    requires (std::is_same_v<StringType, std::string> || std::is_same_v<StringType, std::string_view>)
    bool _do_i_show_caller_next_time_(const StringType & str)
    {
        return (!str.empty() && str.back() == '\n');
    }

    inline bool _do_i_show_caller_next_time_(const char * str)
    {
        return (strlen(str) > 0 && str[strlen(str) - 1] == '\n');
    }

    inline bool _do_i_show_caller_next_time_(const char c)
    {
        return (c == '\n');
    }

    extern class Logger
    {
    private:
        std::mutex log_mutex;
        unsigned int log_level;
        std::atomic_uint filter_level;
        bool endl_found_in_last_log;
        std::ostream * output;
        std::ofstream ofile;
        int container_depth = 0;
        static constexpr int depth_level = 2;
        bool suggest_showing_spaces = true;

    public:
        Logger() noexcept;

    private:
        template <typename ParamType> void _log(const ParamType& param);
        template <typename ParamType, typename... Args>
        void _log(const ParamType& param, const Args&... args);

        template <typename Container>
        requires (is_container_v<Container> && !is_map_v<Container> && !is_unordered_map_v<Container>)
	    void print_container(const Container& container)
        {
            const bool showing_spaces_back = suggest_showing_spaces;
            if (showing_spaces_back) *output << std::string(container_depth * depth_level, ' ');
            container_depth++;
            *output << "[\n";
            for (auto it = std::begin(container); it != std::end(container); ++it)
            {
                *output << std::string(container_depth * depth_level, ' ');
                suggest_showing_spaces = false;
                if (sizeof(*it) == 1 /* 8bit data width */ && !std::is_same_v<decltype(*it), char>)
                {
                    const auto & tmp = *it;
                    *output << "0x" << std::hex << std::setw(2) << std::setfill('0') <<
                        static_cast<unsigned>((*(uint8_t *)(&tmp)) & 0xFF); // ugly workarounds
                }
                else {
                    _log(*it);
                }
                suggest_showing_spaces = true;
                *output << ",\n";
            }
            container_depth--;
            *output << std::string(container_depth * depth_level, ' ');
            *output << "]";
        }

	    template <typename Map>
	    requires (is_map_v<Map> || is_unordered_map_v<Map>)
	    void print_container(const Map & map)
        {
            const bool showing_spaces_back = suggest_showing_spaces;
            if (showing_spaces_back) *output << std::string(container_depth * depth_level, ' ');
            *output << "{\n";
            container_depth++;
            for (auto it = std::begin(map); it != std::end(map); ++it)
            {
                *output << std::string(container_depth * depth_level, ' ');
                suggest_showing_spaces = false;
                _log(it->first);
                container_depth++;
                *output << ":\n" << std::string(container_depth * depth_level, ' ');
                suggest_showing_spaces = false;
                _log(it->second);
                container_depth--;
                *output << ",\n";
            }
            container_depth--;
            *output << std::string(container_depth * depth_level, ' ');
            *output << "}";
        }

        template<typename T> struct is_char_array : std::false_type {};
        template<std::size_t N> struct is_char_array<char[N]> : std::true_type {};
        template<std::size_t N> struct is_char_array<const char[N]> : std::true_type {};

        template<std::size_t... Is, class Tuple>
        constexpr auto tuple_drop_impl(Tuple&& t, std::index_sequence<Is...>) {
            // shift indices by 1 to skip the head
            return std::forward_as_tuple(std::get<Is + 1>(std::forward<Tuple>(t))...);
        }

        template<class Tuple>
        constexpr auto tuple_drop1(Tuple&& t) {
            constexpr std::size_t N = std::tuple_size_v<std::remove_reference_t<Tuple>>;
            static_assert(N > 0, "cannot drop from empty tuple");
            return tuple_drop_impl(std::forward<Tuple>(t), std::make_index_sequence<N - 1>{});
        }

        template <typename T, typename Tag> void _log(const strong_typedef<T, Tag>&) { }

    public:
        template <typename... Args> void log(const Args &...args)
        {
            std::lock_guard lock(log_mutex);
            static_assert(sizeof...(Args) > 0, "log(...) requires at least one argument");
            auto ref_tuple = std::forward_as_tuple(args...);
            using LastType = std::tuple_element_t<sizeof...(Args) - 1, std::tuple<Args...>>;
            using FirstType = std::tuple_element_t<0, std::tuple<Args...>>;
            const std::any last_arg = std::get<sizeof...(Args) - 1>(ref_tuple);
            std::string user_prefix_addon;

            auto call_log = [this]<class... Ts>(Ts&&... xs) -> decltype(auto) {
                return _log(std::forward<Ts>(xs)...);
            };

            if constexpr (std::is_same_v<FirstType, prefix_string_t>) // [PREFIX] [...]
            {
                const std::any first_arg = std::get<0>(ref_tuple);
                user_prefix_addon = std::any_cast<prefix_string_t>(first_arg).value;
            }

            if (endl_found_in_last_log)
            {
                if (!user_prefix_addon.empty()) // Addon and second elements are all present
                {
                    if constexpr (sizeof...(Args) >= 2)
                    {
                        using SecondType = std::tuple_element_t<1, std::tuple<Args...>>;
                        // instead of checking the first, we check the second
                        if constexpr (is_debug_log_t_v<SecondType>) {
                            log_level = 0;
                        }
                        else if constexpr (is_info_log_t_v<SecondType>) {
                            log_level = 1;
                        }
                        else if constexpr (is_warning_log_t_v<SecondType>) {
                            log_level = 2;
                        }
                        else if constexpr (is_error_log_t_v<SecondType>) {
                            log_level = 3;
                        }
                    }
                }
                else // else, we check the first for the presence of log level
                {
                    if constexpr (is_debug_log_t_v<FirstType>) {
                        log_level = 0;
                    }
                    else if constexpr (is_info_log_t_v<FirstType>) {
                        log_level = 1;
                    }
                    else if constexpr (is_warning_log_t_v<FirstType>) {
                        log_level = 2;
                    }
                    else if constexpr (is_error_log_t_v<FirstType>) {
                        log_level = 3;
                    }
                }

                // now, check if the log level is printable or masked
                if (log_level < filter_level) {
                    return;
                }

                const auto now = std::chrono::system_clock::now();
                std::string prefix;
                switch (log_level)
                {
                case 0: prefix =  color::color(1,1,1) + "[DEBUG]"; break;
                case 1: prefix =  color::color(5,5,5) + "[INFO]"; break;
                case 2: prefix =  color::color(5,5,0) + "[WARNING]"; break;
                case 3: prefix =  color::color(5,0,0) + "[ERROR]"; break;
                default: prefix = color::color(5,5,5) + "[INFO]"; break;
                }

                _log(color::color(0, 2, 2), std::format("{:%d-%m-%Y %H:%M:%S}", now), " ",
                    user_prefix_addon,
                    prefix, ": ", color::no_color());
            }

            if constexpr (!is_char_array<LastType>::value && !is_string<LastType>::value) {
                endl_found_in_last_log = false;
            } else if constexpr (!is_char_array<LastType>::value) {
                endl_found_in_last_log = _do_i_show_caller_next_time_(std::any_cast<LastType>(last_arg));
            } else {
                endl_found_in_last_log = _do_i_show_caller_next_time_(std::any_cast<const char*>(last_arg));
            }

            if (user_prefix_addon.empty())
            {
                _log(args...);
            }
            else if constexpr (sizeof...(Args) >= 2)
            {
                std::apply(call_log, tuple_drop1(ref_tuple));
            }
        }
    } cfs_logger;

    template <typename ParamType, typename... Args>
    void Logger::_log(const ParamType& param, const Args &...args)
    {
        _log(param);
        (_log(args), ...);
    }

    template <typename ParamType> void Logger::_log(const ParamType& param)
    {
        // NOLINTBEGIN(clang-diagnostic-repeated-branch-body)
        if constexpr (is_string_v<ParamType>) { // if we don't do it here, it will be assumed as a container
            *output << param;
        }
        else if constexpr (is_container_v<ParamType>) {
            print_container(param);
        }
        else if constexpr (is_bool_v<ParamType>) {
            *output << (param ? "True" : "False");
        }
        else if constexpr (is_pair_v<ParamType>) {
            if (suggest_showing_spaces) *output << std::string(container_depth * depth_level, ' ');
            *output << "<\n";
            container_depth++;
            suggest_showing_spaces = false;
            *output << std::string(container_depth * depth_level, ' ');
            _log(param.first);
            *output << ":\n";
            container_depth++;
            suggest_showing_spaces = false;
            *output << std::string(container_depth * depth_level, ' ');
            _log(param.second);
            *output << "\n";
            container_depth -= 2;
            *output << std::string(container_depth * depth_level, ' ');
            *output << ">";
        }
        else if constexpr (is_debug_log_t_v<ParamType>) {
            log_level = 0;
        }
        else if constexpr (is_info_log_t_v<ParamType>) {
            log_level = 1;
        }
        else if constexpr (is_warning_log_t_v<ParamType>) {
            log_level = 2;
        }
        else if constexpr (is_error_log_t_v<ParamType>) {
            log_level = 3;
        }
        else if constexpr (is_cursor_off_t_v<ParamType>) {
            std::cerr << "\033[?25l";
        }
        else if constexpr (is_cursor_on_t_v<ParamType>) {
            std::cerr << "\033[?25h";
        }
        else if (std::is_same_v<ParamType, unsigned char>)
        {
            *output << "0x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>((*(uint8_t *)(&param)) & 0xFF); // ugly workarounds
        }
        else {
            *output << param;
        }
        // NOLINTEND(clang-diagnostic-repeated-branch-body)
    }

    std::string strip_func_name(std::string);
}

#define logPrint(...)   (void)::cfs::log::cfs_logger.log(cfs::log::prefix_string_t(cfs::color::color(2,3,4) + "(" + cfs::log::strip_func_name(std::source_location::current().function_name()) + ") "), __VA_ARGS__)
#define DEBUG_LOG       (cfs::log::debug_log)
#define INFO_LOG        (cfs::log::info_log)
#define WARNING_LOG     (cfs::log::warning_log)
#define ERROR_LOG       (cfs::log::error_log)

#define debugLogPrint(...)      logPrint(DEBUG_LOG, __VA_ARGS__)
#define infoLogPrint(...)       logPrint(INFO_LOG, __VA_ARGS__)
#define warningLogPrint(...)    logPrint(WARNING_LOG, __VA_ARGS__)
#define errorLogPrint(...)      logPrint(ERROR_LOG, __VA_ARGS__)

#define dlog(...) debugLogPrint(__VA_ARGS__)
#define ilog(...) infoLogPrint(__VA_ARGS__)
#define wlog(...) warningLogPrint(__VA_ARGS__)
#define elog(...) errorLogPrint(__VA_ARGS__)

#define cursor_off  (cfs::log::cursor_off)
#define cursor_on   (cfs::log::cursor_on)

#endif //CFS_LOGGER_H
