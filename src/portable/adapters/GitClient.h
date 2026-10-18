// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QStringList>
#include <functional>

namespace tgit
{
struct GitResult
{
	QByteArray output;
	QByteArray error;
	int exitCode = -1;
	bool cancelled = false;
	bool ok() const { return !cancelled && exitCode == 0; }
	QString message() const;
};

class GitClient : public QObject
{
	Q_OBJECT
public:
	using Callback = std::function<void(const GitResult&)>;
	explicit GitClient(QObject* parent = nullptr);
	~GitClient() override;
	void execute(const QString& directory, const QStringList& arguments, Callback callback,
		const QByteArray& input = {}, int timeoutMs = 300000);
	void cancel();
	bool busy() const { return m_process || !m_queue.isEmpty(); }
	QString executable() const { return m_executable; }
	void setExecutable(const QString& executable) { m_executable = executable; }
	signals:
	void busyChanged(bool busy);
	void commandStarted(const QStringList& arguments);
	void diagnostic(const QString& text);
	private:
	struct Request
	{
		QString directory;
		QStringList arguments;
		Callback callback;
		QByteArray input;
		int timeoutMs;
	};
	void startNext();
	void complete(int exitCode);
	void drain();
	void stopProcess();
	QQueue<Request> m_queue;
	Request m_active;
	QProcess* m_process = nullptr;
	GitResult m_result;
	QString m_executable;
	bool m_stopping = false;
	qint64 m_processGroup = 0;
	static constexpr qsizetype OutputLimit = 32 * 1024 * 1024;
};
}
