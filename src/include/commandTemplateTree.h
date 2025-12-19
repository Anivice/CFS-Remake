#ifndef CFS_COMMANDTEMPLATETREE_H
#define CFS_COMMANDTEMPLATETREE_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "utils.h"
#include "generalCFSbaseError.h"

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
    std::string gen_cmd(const unsigned char * src, unsigned int len);

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
    const NodeType * find(const NodeType & entry, const std::string & name);

    /// find target node by command path
    /// @param root tree root
    /// @param command_string command path
    /// @return reference of the target node
    /// @throws cfs::error::command_not_found Provided command path doesn't have a match
    const NodeType * find(const NodeType & root, const std::vector < std::string > & command_string);

    class commandTemplateTree_t {
    public:
        static constexpr auto host_system_path = "<HOST SYSTEM PATH>";
        static constexpr auto cfs_path = "<CFS PATH>";
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
        const NodeType * find(const std::vector < std::string > & command_string) const {
            return cmdTpTree::find(root, command_string);
        }

        /// find subcommands and verbs by command path
        /// @param command_string command path
        /// @return acceptable commands
        /// @throws cfs::error::command_not_found Provided command path doesn't have a match
        std::vector < std::string > find_sub_commands(const std::vector < std::string > & command_string) const
        {
            const auto * node = cmdTpTree::find(root, command_string);
            std::vector < std::string > result;
            for (const auto & v : node->children_) {
                result.push_back(v->name_);
            }

            return result;
        }

        /// get help
        /// @param command_string command path
        /// @return help text
        /// @throws cfs::error::command_not_found Provided command path doesn't have a match
        std::string get_help(const std::vector < std::string > & command_string) const
        {
            const auto * node = cmdTpTree::find(root, command_string);
            return node->help_text_;
        }
    };
} // cmdTpTree

#endif //CFS_COMMANDTEMPLATETREE_H