// SPDX-License-Identifier: GPL-2.0-or-later
#include "StatusModel.h"
#include <QBrush>
#include <QColor>

int StatusModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_status.entries.size());
}

int StatusModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 4; }

bool StatusModel::usablePath(const std::string& path)
{
	return QString::fromUtf8(path.data(), path.size()).toUtf8().toStdString() == path;
}

QString StatusModel::displayPath(const std::string& path)
{
	return QString::fromUtf8(path.data(), path.size()).replace('\\', "\\\\").replace('\n', "\\n").replace('\r', "\\r").replace('\t', "\\t");
}

QVariant StatusModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= rowCount())
		return {};
	const auto& item = entry(index.row());
	if (role == Qt::ToolTipRole)
		return usablePath(item.path) ? displayPath(item.path) : tr("This name cannot be represented losslessly as UTF-8. Modify it with Git in Terminal.");
	if (role == Qt::ForegroundRole && item.conflicted)
		return QBrush(QColor("#c0392b"));
	if (role != Qt::DisplayRole)
		return {};
	switch (index.column())
	{
	case 0:
		if (item.conflicted) return tr("Conflict");
		if (item.untracked) return tr("Untracked");
		if (item.staged() && item.unstaged()) return tr("Staged + modified");
		return item.staged() ? tr("Staged") : tr("Modified");
	case 1:
		return item.originalPath.empty() ? displayPath(item.path) : displayPath(item.originalPath) + " → " + displayPath(item.path);
	case 2: return QString(QChar(item.index));
	case 3: return QString(QChar(item.worktree));
	default: return {};
	}
}

QVariant StatusModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};
	const QStringList headers{tr("Status"), tr("Path"), tr("Index"), tr("Worktree")};
	return headers.value(section);
}

void StatusModel::setStatus(tgit::RepositoryStatus status)
{
	beginResetModel();
	m_status = std::move(status);
	endResetModel();
}
