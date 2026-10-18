// SPDX-License-Identifier: GPL-2.0-or-later
#include "GitClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <utility>
#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

namespace tgit
{
QString GitResult::message() const
{
	if (cancelled)
		return QStringLiteral("Operation cancelled. Check the repository for any changes already made.");
	const auto details = QString::fromUtf8(error.isEmpty() ? output : error).trimmed();
	return details.isEmpty() ? QStringLiteral("Git exited with code %1").arg(exitCode) : details;
}

GitClient::GitClient(QObject* parent) : QObject(parent), m_executable(QStandardPaths::findExecutable("git"))
{
#ifdef Q_OS_MACOS
	if (m_executable.isEmpty() && QFileInfo::exists("/usr/bin/git"))
		m_executable = "/usr/bin/git";
#endif
}

GitClient::~GitClient()
{
	if (m_process)
	{
		m_process->disconnect(this);
		stopProcess();
#ifdef Q_OS_UNIX
		if (m_processGroup > 0) ::kill(-static_cast<pid_t>(m_processGroup), SIGKILL);
#endif
		m_process->kill();
		m_process->waitForFinished(1500);
	}
}

void GitClient::execute(const QString& directory, const QStringList& arguments, Callback callback,
	const QByteArray& input, int timeoutMs)
{
	const bool wasBusy = busy();
	m_queue.enqueue({directory, arguments, std::move(callback), input, timeoutMs});
	if (!wasBusy)
		emit busyChanged(true);
	if (!m_process)
		startNext();
}

void GitClient::startNext()
{
	if (m_queue.isEmpty() || m_process)
		return;
	m_active = m_queue.dequeue();
	m_result = {};
	m_stopping = false;
	m_processGroup = 0;
	auto* process = new QProcess(this);
	m_process = process;
	process->setProgram(m_executable);
	QStringList arguments{"--literal-pathspecs", "-c", "color.ui=false", "-c", "core.quotepath=false",
		"-c", "i18n.logOutputEncoding=UTF-8"};
	arguments.append(m_active.arguments);
	process->setArguments(arguments);
	process->setWorkingDirectory(m_active.directory);
	auto environment = QProcessEnvironment::systemEnvironment();
	// A GUI launched from a hook/terminal must still operate on the explicitly opened repository.
	for (const auto& key : {"GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR", "GIT_PREFIX",
		"GIT_NAMESPACE", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES"})
		environment.remove(key);
	environment.insert("GIT_TERMINAL_PROMPT", "0");
	environment.insert("GIT_OPTIONAL_LOCKS", "0");
	const QString helper = QCoreApplication::applicationDirPath() + "/TortoiseGitAskPass";
	if (QFileInfo::exists(helper))
	{
		environment.insert("GIT_ASKPASS", helper);
		environment.insert("SSH_ASKPASS", helper);
		environment.insert("SSH_ASKPASS_REQUIRE", "force");
	}
	process->setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
	QProcess::UnixProcessParameters parameters;
	parameters.flags = QProcess::UnixProcessFlag::CreateNewSession;
	process->setUnixProcessParameters(parameters);
#endif
	connect(process, &QProcess::started, this, [this, process]
	{
		m_processGroup = process->processId();
		if (!m_active.input.isEmpty())
			process->write(m_active.input);
		process->closeWriteChannel();
	});
	connect(process, &QProcess::readyReadStandardOutput, this, &GitClient::drain);
	connect(process, &QProcess::readyReadStandardError, this, &GitClient::drain);
	connect(process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status)
	{
		complete(status == QProcess::NormalExit ? code : -1);
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error)
	{
		if (error == QProcess::FailedToStart)
		{
			m_result.error = process->errorString().toUtf8();
			complete(-1);
		}
	});
	if (m_active.timeoutMs > 0)
		QTimer::singleShot(m_active.timeoutMs, process, [this, process]
		{
			if (m_process == process)
			{
				m_result.error += "\nOperation timed out.";
				stopProcess();
			}
		});
	emit commandStarted(m_active.arguments);
	process->start();
}

void GitClient::drain()
{
	if (!m_process)
		return;
	const auto output = m_process->readAllStandardOutput();
	const auto error = m_process->readAllStandardError();
	const bool overflow = output.size() > OutputLimit - m_result.output.size()
		|| error.size() > OutputLimit - m_result.error.size();
	m_result.output += output.left(qMax<qsizetype>(0, OutputLimit - m_result.output.size()));
	m_result.error += error.left(qMax<qsizetype>(0, OutputLimit - m_result.error.size()));
	if (overflow && !m_stopping)
	{
		m_result.error = "Output exceeded the 32 MiB preview limit. Narrow the selection and retry.";
		stopProcess();
	}
}

void GitClient::complete(int exitCode)
{
	if (!m_process)
		return;
	drain();
#ifdef Q_OS_UNIX
	// Git may exit before a hook/filter child that ignored SIGTERM. Reap that session too.
	if (m_stopping && m_processGroup > 0) ::kill(-static_cast<pid_t>(m_processGroup), SIGKILL);
#endif
	m_processGroup = 0;
	auto* process = m_process;
	m_process = nullptr;
	process->disconnect(this);
	process->deleteLater();
	m_result.exitCode = m_stopping ? -1 : exitCode;
	const auto result = m_result;
	const auto callback = std::move(m_active.callback);
	if (!result.error.isEmpty())
		emit diagnostic(QString::fromUtf8(result.error));
	// Defer callbacks so a newly queued request cannot replace m_active during completion.
	QTimer::singleShot(0, this, [this, callback, result]
	{
		if (callback)
			callback(result);
		if (!m_process)
		{
			if (!m_queue.isEmpty())
				startNext();
			else
				emit busyChanged(false);
		}
	});
}

void GitClient::stopProcess()
{
	if (!m_process || m_stopping)
		return;
	m_stopping = true;
	const auto pid = m_process->processId();
#ifdef Q_OS_UNIX
	if (pid > 0)
		::kill(-static_cast<pid_t>(pid), SIGTERM);
#endif
	m_process->terminate();
	const QPointer<QProcess> process(m_process);
	QTimer::singleShot(1200, this, [this, process, pid]
	{
		if (!process || m_process != process)
			return;
#ifdef Q_OS_UNIX
		if (pid > 0)
			::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
		process->kill();
	});
}

void GitClient::cancel()
{
	// Queued reads and dependent operations must not continue after cancellation.
	const auto queued = std::exchange(m_queue, {});
	for (const auto& request : queued)
	{
		GitResult cancelled;
		cancelled.cancelled = true;
		QTimer::singleShot(0, this, [request, cancelled] { if (request.callback) request.callback(cancelled); });
	}
	m_result.cancelled = true;
	stopProcess();
}
}
