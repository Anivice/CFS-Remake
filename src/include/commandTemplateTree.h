#ifndef CFS_COMMANDTEMPLATETREE_H
#define CFS_COMMANDTEMPLATETREE_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "smart_block_t.h"

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
    };

    /// for_each handler, constraint it to be accepting only void(const Readline::NodeType&, int)
    template <typename F>
    concept Function = requires(F f, const NodeType& a, int d) {
        { std::invoke(f, a, d) } -> std::same_as<void>;
    };

    /// loop the whole command template tree
    /// @param entry tree root
    /// @param func_ node handler
    /// @param depth venture depth. if (depth & 0x01) is true, i.e., odd number, the node is a verb.
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
} // cmdTpTree

#endif //CFS_COMMANDTEMPLATETREE_H