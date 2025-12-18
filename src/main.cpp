#include <iostream>
#include <algorithm>
#include <memory>
#include <sstream>
#include "colors.h"
#include "generalCFSbaseError.h"
#include "utils.h"
#include "args.h"
#include "cfs.h"
#include "routes.h"
#include "readline.h"
#include "history.h"
#include "smart_block_t.h"

namespace utils = cfs::utils;

utils::PreDefinedArgumentType::PreDefinedArgument MainArgument = {
    { .short_name = 'h', .long_name = "help",       .argument_required = false, .description = "Show help" },
    { .short_name = 'v', .long_name = "version",    .argument_required = false, .description = "Show version" },
    { .short_name = 'b', .long_name = "block_file", .argument_required = true,  .description = "CFS target path" },
};

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

class Readline {
private:
    class argumentTree_t {
    public:
        static constexpr auto host_system_path = "<HOST SYSTEM PATH>";
        static constexpr auto cfs_path = "<CFS PATH>";
        NodeType root;

    protected:

        void construct(const std::string & command_description)
        {
            /*
             * { < command: < subcommand1: verb1, <subcommand1.1, verb1 > >, verb2, verb3, < subcommand2: verb1> >, # ignored util '\n'
             *   < command2: [HSP], [CFSP] > }
             */

            auto remove_comments = [](const std::string & text)->std::string
            {
                std::stringstream ss(text), output;
                std::string line;
                while (std::getline(ss, line))
                {
                    const auto pos = line.find_first_of('#');
                    if (pos != std::string::npos) {
                        line = line.substr(0, pos);
                    }

                    output << line << std::endl;
                }

                return output.str();
            };

            std::stringstream ss(remove_comments(command_description));
            std::vector < frame_t > stack;
            stack.emplace_back();
            stack.back().entry_ = &root;
            bool help_text_override = false;

            char c;
            while (ss && ((  c = static_cast<char>( ss.get())  )) )
            {
                std::string & command_ = stack.back().command_;
                std::string & verb_ = stack.back().verb_;
                std::vector < std::string > & verbs_ = stack.back().verbs_;
                std::string & help_text_ = stack.back().help_text_;
                NodeType * entry = stack.back().entry_;
                CurrentStatusType & status = stack.back().status_;

                if (status == EndLoop) break;

                if (help_text_override)
                {
                    if (c != ')') {
                        help_text_ += c;
                    } else {
                        help_text_override = false;
                    }

                    continue;
                }

                if (c == '(') {
                    help_text_override = true;
                    continue;
                }

                switch (c)
                {
                    case '{':
                        continue;
                    case '}': {
                        status = EndLoop;
                        continue;
                    }
                    case '<': {
                        const auto parent = entry;
                        parent->children_.emplace_back(std::make_unique<NodeType>());
                        const auto child = entry->children_.back().get();
                        child->parent_ = parent;
                        if (status == NoOperation) status = ReadingCommand;

                        stack.emplace_back();
                        stack.back().entry_ = child;
                        stack.back().status_ = ReadingCommand;
                        continue;
                    }
                    case ':': {
                        status = ReadingVerbs;
                        continue;
                    }
                    case '>': {
                        if (!verb_.empty()) verbs_.push_back(verb_);

                        entry->help_text_ = help_text_;
                        entry->name_ = command_;
                        for (const auto & v : verbs_)
                        {
                            entry->children_.emplace_back(std::make_unique< NodeType >(NodeType{
                                .name_ = v,
                                .help_text_ = help_text_,
                                .children_ = {},
                                .parent_ = entry,
                            }));
                        }

                        stack.pop_back();
                        continue;
                    }
                    default: break;
                }

                switch (status)
                {
                    case ReadingCommand: {
                        if (c == ' ') continue;
                        command_ += c;
                        break;
                    }

                    case ReadingVerbs:
                    {
                        if (c == ' ') continue;
                        if (c != ',') {
                            verb_ += c;
                        } else if (!verb_.empty()) {
                            verbs_.push_back(verb_);
                            verb_.clear();
                        }
                        break;
                    }

                    case NoOperation: {
                        continue;
                    }

                    default: elog("Unparsed character: `", c, "'\n");
                }
            }
        }

    public:
        NO_COPY_OBJ(argumentTree_t);

        argumentTree_t(const std::string & str) { construct(str); }
        argumentTree_t(const char * str) { construct(str); }
        template < unsigned N > argumentTree_t (const char str[N]) { construct(str); }
    };
public:

    argumentTree_t argumentTree =
        " { "
        "   < command (Help text): < subcommand1 (Help text2): verb1, <subcommand1.1: verb1>, verb2, verb3 >, "
        "              < subcommand2: verb1 >, "
        "   >, # comments are ignored util '\\n'\n"
        "   < command2 (Help text3 ): [HSP], [CFSP] > "
        " } ";
};

void print_line(const NodeType * entry, const int depth)
{
    if (entry->name_.empty()) {
        std::cout << "{" << std::endl;
    }
    else {
        std::cout << std::string(depth * 2, ' ') << entry->name_ << ":" << std::endl;
    }

    for (const auto & v : entry->children_)
    {
        if (v->children_.empty()) { // doesn't have verbs, so normal command
            std::cout << std::string(depth * 2 + 2, ' ') << "=> " << v->name_ << std::endl;
        } else { // has verbs, so subcommands
            print_line(v.get(), depth + 1);
        }
    }

    if (entry->name_.empty()) {
        std::cout << "}" << std::endl;
    }
};

int main(int argc, char** argv)
{
    try
    {
        auto basename = [](const std::string & name)->std::string
        {
            if (const auto pos = name.find_last_of('/');
                pos != std::string::npos)
            {
                return name.substr(pos + 1);
            }
            return name;
        };

        const auto route = basename(argv[0]);

        if (route == "fsck.cfs") {
            return fsck_main(argc, argv);
        }

        if (route == "mount.cfs") {
            return mount_main(argc, argv);
        }

        if (route == "mkfs.route") {
            return mkfs_main(argc, argv);
        }

        /// No routes detected, utility mode
        ilog("CFS utility version " CFS_IMPLEMENT_VERSION " (standard revision number " CFS_STANDARD_VERSION ")\n");
        const utils::PreDefinedArgumentType PreDefinedArguments(MainArgument);
        utils::ArgumentParser ArgumentParser(argc, argv, PreDefinedArguments);
        const auto parsed = ArgumentParser.parse();
        if (parsed.contains("help")) {
            std::cout << *argv << " [Arguments [OPTIONS...]...]" << std::endl;
            std::cout << PreDefinedArguments.print_help();
            return EXIT_SUCCESS;
        }

        if (parsed.contains("version")) {
            std::cout << *argv << std::endl;
            std::cout.write(reinterpret_cast<const char *>(version_text), version_text_len);
            std::cout << std::endl;
            return EXIT_SUCCESS;
        }

        Readline readline;
        print_line(&readline.argumentTree.root, 0);
        __asm__("nop");
    }
    catch (cfs::error::generalCFSbaseError & e) {
        elog(e.what(), "\n");
    }
    catch (std::exception & e) {
        elog(e.what(), "\n");
    }

    return 0;
}
