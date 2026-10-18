// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDialog>
#include <QMap>
#include <QStringList>
class QComboBox;
class QLineEdit;

// Fields are structured values, never a command line interpreted by a shell.
class OperationDialog : public QDialog
{
public:
	explicit OperationDialog(const QString& title, const QString& description, QWidget* parent = nullptr);
	void addText(const QString& key, const QString& label, const QString& value = {});
	void addChoice(const QString& key, const QString& label, const QStringList& values);
	QString value(const QString& key) const;
private:
	class QFormLayout* m_form;
	QMap<QString, QLineEdit*> m_text;
	QMap<QString, QComboBox*> m_choices;
};
