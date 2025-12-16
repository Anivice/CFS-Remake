/* execute.h
 *
 * Copyright 2025 Anivice Ives
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef CFS_EXECUTE_H
#define CFS_EXECUTE_H

#include <string>
#include <vector>

namespace cfs::utils
{
    /// command's status
    struct cmd_status
    {
        std::string fd_stdout; // normal output
        std::string fd_stderr; // error information
        int exit_status{}; // exit status
    };

    /// Execute an external process
    /// @param cmd Full path to executable
    /// @param args Arguments
    /// @param input Child's stdin
    /// @return Command's exit status, stdout, and stderr
    cmd_status exec_command_(const std::string &cmd, const std::vector<std::string> &args, const std::string &input);

    /// Execute an external process (template argument wrapper)
    /// @param cmd Full path to executable
    /// @param input Child's stdin
    /// @param args Command's arguments
    /// @return Command's exit status, stdout, and stderr
    /// @example ../../example/execute.cpp
    template <typename... Strings>
    cmd_status exec_command(const std::string& cmd, const std::string & input = "", Strings &&... args)
    {
        const std::vector<std::string> vec{std::forward<Strings>(args)...};
        return exec_command_(cmd, vec, input);
    }
}

#endif //CFS_EXECUTE_H