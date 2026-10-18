// SPDX-License-Identifier: GPL-2.0-or-later
#include "RepositoryStatus.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool value, const char* description)
{
	if (!value) throw std::runtime_error(description);
}
std::string records(std::initializer_list<std::string> values)
{
	std::string output;
	for (const auto& value : values) { output += value; output += '\0'; }
	return output;
}
}

int main()
{
	try
	{
		const auto parsed = tgit::parseStatus(records({"# branch.oid (initial)", "# branch.head qt",
			"# branch.upstream origin/qt", "# branch.ab +3 -2", "? 한글 file\nwith\ttab.txt",
			"1 MM N... 100644 100644 100644 abc def tracked.txt",
			"2 R. N... 100644 100644 100644 abc def R100 new file.txt", "old\nfile.txt",
			"u UU N... 100644 100644 100644 100644 a b c conflict.txt"}));
		require(static_cast<bool>(parsed), "valid records rejected");
		require(parsed.status.unborn && parsed.status.branch == "qt", "branch headers");
		require(parsed.status.upstream == "origin/qt" && parsed.status.ahead == 3 && parsed.status.behind == 2, "tracking headers");
		require(parsed.status.entries.size() == 4, "record count");
		require(parsed.status.entries[0].path == "한글 file\nwith\ttab.txt" && parsed.status.entries[0].untracked, "literal path bytes");
		require(parsed.status.entries[1].staged() && parsed.status.entries[1].unstaged(), "mixed status");
		require(parsed.status.entries[2].originalPath == "old\nfile.txt" && parsed.status.entries[2].path == "new file.txt", "rename record");
		require(parsed.status.entries[3].conflicted, "unmerged record");
		require(!tgit::parseStatus("? truncated"), "truncated data accepted");
		require(!tgit::parseStatus(records({"1 M."})), "missing fields accepted");
		require(!tgit::parseStatus(records({"2 R. N... 100644 100644 100644 a b R100 new"})), "missing rename source accepted");
		require(!tgit::parseStatus(records({"? "})), "empty path accepted");
		require(!tgit::parseStatus(records({"unknown"})), "unknown record accepted");
		std::string raw("bad\xffname", 8);
		const auto invalidUtf8 = tgit::parseStatus(records({"? " + raw}));
		require(invalidUtf8.status.entries[0].path == raw, "non-UTF-8 bytes lost");
		require(tgit::parseStatus(records({"! ignored"})).status.entries.empty(), "ignored entry surfaced");
		std::cout << "Status parser: paths, headers, rename, conflicts and malformed input passed\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
