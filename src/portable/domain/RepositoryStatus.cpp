// SPDX-License-Identifier: GPL-2.0-or-later
#include "RepositoryStatus.h"

#include <charconv>

namespace tgit
{
StatusParseResult parseStatus(std::string_view output)
{
	StatusParseResult result;
	auto takeRecord = [&output](std::string_view& record)
	{
		const auto end = output.find('\0');
		if (end == std::string_view::npos)
			return false;
		record = output.substr(0, end);
		output.remove_prefix(end + 1);
		return true;
	};
	while (!output.empty())
	{
		std::string_view record;
		if (!takeRecord(record) || record.empty())
		{
			result.error = "Truncated or empty status record";
			break;
		}
		if (record.starts_with("# "))
		{
			auto header = [&record](std::string_view key) { return record.substr(key.size()); };
			if (record.starts_with("# branch.head "))
				result.status.branch = header("# branch.head ");
			else if (record.starts_with("# branch.upstream "))
				result.status.upstream = header("# branch.upstream ");
			else if (record.starts_with("# branch.oid "))
			{
				result.status.oid = header("# branch.oid ");
				result.status.unborn = result.status.oid == "(initial)";
			}
			else if (record.starts_with("# branch.ab +"))
			{
				const auto numbers = header("# branch.ab +");
				const auto split = numbers.find(" -");
				if (split != std::string_view::npos)
				{
					std::from_chars(numbers.data(), numbers.data() + split, result.status.ahead);
					std::from_chars(numbers.data() + split + 2, numbers.data() + numbers.size(), result.status.behind);
				}
			}
			continue;
		}
		StatusEntry entry;
		if (record.starts_with("? ") || record.starts_with("! "))
		{
			if (record.front() == '!')
				continue;
			entry.path = record.substr(2);
			entry.untracked = true;
			entry.index = entry.worktree = '?';
		}
		else if (record.starts_with("1 ") || record.starts_with("2 ") || record.starts_with("u "))
		{
			const int fields = record.front() == '1' ? 8 : record.front() == '2' ? 9 : 10;
			std::size_t offset = 0;
			for (int i = 0; i < fields; ++i)
			{
				const auto space = record.find(' ', offset);
				if (space == std::string_view::npos)
				{
					result.error = "Malformed tracked status record";
					return result;
				}
				offset = space + 1;
			}
			if (record.size() < 5 || record[4] != ' ')
			{
				result.error = "Malformed status flags";
				return result;
			}
			entry.index = record[2];
			entry.worktree = record[3];
			entry.conflicted = record.front() == 'u';
			entry.path = record.substr(offset);
			if (record.front() == '2')
			{
				std::string_view original;
				if (!takeRecord(original) || original.empty())
				{
					result.error = "Missing rename source";
					return result;
				}
				entry.originalPath = original;
			}
		}
		else
		{
			result.error = "Unknown status record";
			return result;
		}
		if (entry.path.empty())
		{
			result.error = "Empty status path";
			return result;
		}
		result.status.entries.push_back(std::move(entry));
	}
	return result;
}
}
