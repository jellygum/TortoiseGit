// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tgit
{
struct StatusEntry
{
	std::string path;
	std::string originalPath;
	char index = '.';
	char worktree = '.';
	bool untracked = false;
	bool conflicted = false;
	bool staged() const { return !untracked && index != '.'; }
	bool unstaged() const { return untracked || worktree != '.'; }
};

struct RepositoryStatus
{
	std::string branch;
	std::string upstream;
	std::string oid;
	int ahead = 0;
	int behind = 0;
	bool unborn = false;
	std::vector<StatusEntry> entries;
};

struct StatusParseResult
{
	RepositoryStatus status;
	std::string error;
	explicit operator bool() const { return error.empty(); }
};

// Porcelain v2 -z paths are opaque bytes, including spaces and newlines.
StatusParseResult parseStatus(std::string_view output);
}
