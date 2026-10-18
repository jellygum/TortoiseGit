// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "RepositoryStatus.h"
#include <QAbstractTableModel>

class StatusModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	explicit StatusModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	void setStatus(tgit::RepositoryStatus status);
	const tgit::RepositoryStatus& status() const { return m_status; }
	const tgit::StatusEntry& entry(int row) const { return m_status.entries.at(static_cast<std::size_t>(row)); }
	static QString displayPath(const std::string& path);
	static bool usablePath(const std::string& path);
private:
	tgit::RepositoryStatus m_status;
};
