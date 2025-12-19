#include "commandTemplateTree.h"
#include "logger.h"
#include <sstream>
#include <cstring>

namespace cmdTpTree
{
    std::string gen_cmd(const unsigned char *src, const unsigned int len)
    {
        std::vector<char> data;
        data.resize(len + 1, 0);
        std::memcpy(data.data(), src, len);
        std::string ret = data.data();
        return ret;
    }

    void commandTemplateTree_t::construct(const std::string &command_description)
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
            if (c < 0x20) continue; // ignore '\n' and all
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
} // cmdTpTree