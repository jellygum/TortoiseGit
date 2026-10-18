// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <cstdio>

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);
	QApplication::setApplicationName("TortoiseGit Authentication");
	const auto prompt = app.arguments().value(1, "Git credential:");
	const bool secret = !prompt.contains("username", Qt::CaseInsensitive)
		&& !prompt.contains("yes/no", Qt::CaseInsensitive);
	bool accepted = false;
	const auto response = QInputDialog::getText(nullptr, "TortoiseGit Authentication", prompt,
		secret ? QLineEdit::Password : QLineEdit::Normal, {}, &accepted);
	if (!accepted)
		return 1;
	const auto bytes = response.toUtf8();
	std::fwrite(bytes.constData(), 1, bytes.size(), stdout);
	std::fputc('\n', stdout);
	return 0;
}
