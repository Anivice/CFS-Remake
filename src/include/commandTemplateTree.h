#ifndef CFS_COMMANDTEMPLATETREE_H
#define CFS_COMMANDTEMPLATETREE_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "utils.h"
#include "generalCFSbaseError.h"
#include <thread>
#include "readline/readline.h"
#include "readline/history.h"
#include <csignal>

make_simple_error_class(command_not_found)

namespace cmdTpTree
{
    enum CurrentStatusType : int { NoOperation = 0, ReadingCommand, ReadingVerbs, EndLoop };

    struct NodeType {
        std::string name_;
        std::string help_text_;
        std::vector < std::unique_ptr < NodeType > > children_;
        NodeType * parent_ = nullptr;
    };

    struct frame_t {
        std::string command_;
        std::string verb_;
        std::vector < std::string > verbs_;
        std::string help_text_;
        NodeType * entry_ = nullptr;
        CurrentStatusType status_ = NoOperation;
    };

    /// convert from unsigned char [] with length to std::string
    /// @param src unsigned char[] pointer
    /// @param len array length
    /// @return parsed std::string
    [[nodiscard]] std::string gen_cmd(const unsigned char * src, unsigned int len);

    /// for_each handler, constraint it to be accepting only void(const Readline::NodeType&, int)
    template <typename F>
    concept Function = requires(F f, const NodeType& a, int d) {
        { std::invoke(f, a, d) } -> std::same_as<void>;
    };

    /// loop the whole command template tree
    /// @param entry tree root
    /// @param func_ node handler
    /// @param depth venture depth.
    /// You don't supply it here but depth will be submitted to the provided handler.
    /// if (depth & 0x01) is true, i.e., odd number, the node is a verb.
    /// otherwise it's a subcommand that has its own set of verbs
    /// @tparam function implied node handler type
    template < Function function >
    void for_each(const NodeType & entry, function func_, const int depth = 0)
    {
        // put func(..., depth - 2) because root is empty, so first depth, depth 0 will be depth 2 is unhandled
        if (!entry.name_.empty()) {
            func_(entry, depth - 2);
        }

        for (const auto & v : entry.children_)
        {
            if (v->children_.empty()) { // doesn't have verbs, so normal command
                func_(*v, depth + 1 - 2);
            } else { // has verbs, so subcommands
                for_each<function>(*v, func_, depth + 2);
            }
        }
    }

    /// find node in children by name
    /// @param entry parent
    /// @param name child's name
    /// @return NodeType pointer, or NULL when not found
    [[nodiscard]] const NodeType * find(const NodeType & entry, const std::string & name);

    /// find target node by command path
    /// @param root tree root
    /// @param command_string command path
    /// @return reference of the target node
    /// @throws cfs::error::command_not_found Provided command path doesn't have a match
    [[nodiscard]] const NodeType * find(const NodeType & root, const std::vector < std::string > & command_string);

    constexpr auto host_system_path = "[HSP]";
    constexpr auto cfs_path = "[CFSP]";
    constexpr auto no_subcommands = "[NONE]";
    constexpr auto arbitrary_length = "[ARB]";

    extern class commandTemplateTree_t {
    public:
        NodeType root;

    protected:

        /// construct a command template tree
        /// @param command_description command source
        void construct(const std::string & command_description);

    public:
        NO_COPY_OBJ(commandTemplateTree_t);

        commandTemplateTree_t(const std::string & str) { construct(str); }
        commandTemplateTree_t(const char * str) { construct(str); }
        template < unsigned N > commandTemplateTree_t (const char str[N]) { construct(str); }

        /// loop the whole command template tree
        /// @param func_ node handler
        /// @tparam function implied node handler type
        template < Function function >
        void for_each(function func_) {
            cmdTpTree::for_each(root, func_);
        }

        /// find target node by command path
        /// @param command_string command path
        /// @return reference of the target node
        /// @throws cfs::error::command_not_found Provided command path doesn't have a match
        [[nodiscard]] const NodeType * find(const std::vector < std::string > & command_string) const;

        /// find subcommands and verbs by command path
        /// @param command_string command path
        /// @return acceptable commands
        /// @throws cfs::error::command_not_found Provided command path doesn't have a match
        [[nodiscard]] std::vector < std::string > find_sub_commands(const std::vector < std::string > & command_string) const;

        /// get help
        /// @param command_string command path
        /// @return help text
        /// @throws cfs::error::command_not_found Provided command path doesn't have a match
        [[nodiscard]] std::string get_help(const std::vector < std::string > & command_string) const;
    } command_template_tree;

    /// for_each handler, constraint it to be accepting only void(const Readline::NodeType&, int)
    template <typename F>
    concept CommandHandler = requires(F f, const std::vector < std::string > & command_string) {
        { std::invoke(f, command_string) } -> std::same_as<void>;
    };

    char ** cmd_completion(const char *text, int start, int end);

    template < CommandHandler handler >
    void read_command(handler handler_, const std::string & prompt)
    {
        pthread_setname_np(pthread_self(), "readline");

        auto remove_leading_and_tailing_spaces = [](const std::string & text)->std::string
        {
            if (text.empty()) return text;
            const auto pos = text.find_first_not_of(' ');
            if (pos == std::string::npos) return text;
            std::string middle = text.substr(pos);
            while (!middle.empty() && middle.back() == ' ') {
                middle.pop_back();
            }
            return middle;
        };

        rl_attempted_completion_function = cmd_completion;
        using_history();
        std::string last_line;

        char * line = nullptr;
        while ((line = readline(prompt.c_str())) != nullptr)
        {
            try {
                std::vector < std::string > command_vector;
                {
                    /// save history, and simple dedup
                    const auto presented_history = remove_leading_and_tailing_spaces(line);
                    if (*line && presented_history != last_line) {
                        add_history(presented_history.c_str());
                    }

                    if (!presented_history.empty()) last_line = presented_history;

                    /// compose a command vector
                    std::string cmd = line;
                    cmd = remove_leading_and_tailing_spaces(cmd);
                    std::string buffer;
                    bool override = false;
                    bool escape = false;
                    for (const auto c : cmd)
                    {
                        if (c == '\\') {
                            escape = true;
                            continue;
                        }

                        if (escape) {
                            buffer += c;
                            escape = false;
                            continue;
                        }

                        if (c == '"') {
                            override = !override;
                            continue;
                        }

                        if (override)
                        {
                            buffer += c;
                            continue;
                        }

                        if (c != ' ') {
                            buffer.push_back(c);
                        }
                        else if (!buffer.empty())
                        {
                            command_vector.push_back(buffer);
                            buffer.clear();
                        }
                    }

                    if (!buffer.empty()) {
                        command_vector.push_back(buffer);
                        buffer.clear();
                    }
                }
                free(line);
                handler_(command_vector);
            } catch (const std::exception & e) {
                elog(e.what(), "\n");
            }
        }
    }
} // cmdTpTree

#endif //CFS_COMMANDTEMPLATETREE_H