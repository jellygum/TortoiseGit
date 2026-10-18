// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QSyntaxHighlighter>

class DiffHighlighter : public QSyntaxHighlighter
{
public:
	explicit DiffHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}
protected:
	void highlightBlock(const QString& text) override;
};
