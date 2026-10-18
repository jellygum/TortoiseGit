// SPDX-License-Identifier: GPL-2.0-or-later
#include "MainWindow.h"
#include "DiffHighlighter.h"
#include "OperationDialog.h"
#include "StatusModel.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
QString decode(const std::string& value) { return QString::fromUtf8(value.data(), value.size()); }
QString oneLine(QByteArray value)
{
	if (value.endsWith('\n')) value.chop(1);
	return QString::fromUtf8(value);
}
QString redact(QString text)
{
	return text.replace(QRegularExpression("(https?://)[^/@\\s]+@"), "\\1[credentials]@");
}
void notice(QWidget* parent, const QString& title, const QString& text, QMessageBox::Icon icon = QMessageBox::Information)
{
	QMessageBox dialog(icon, title, redact(text), QMessageBox::Ok, parent);
	dialog.setTextFormat(Qt::PlainText);
	dialog.exec();
}
QPlainTextEdit* preview(QWidget* parent)
{
	auto* edit = new QPlainTextEdit(parent);
	edit->setReadOnly(true);
	edit->setLineWrapMode(QPlainTextEdit::NoWrap);
	edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	new DiffHighlighter(edit->document());
	return edit;
}
QTableWidget* table(const QStringList& headers, QWidget* parent)
{
	auto* widget = new QTableWidget(0, headers.size(), parent);
	widget->setHorizontalHeaderLabels(headers);
	widget->setSelectionBehavior(QAbstractItemView::SelectRows);
	widget->setSelectionMode(QAbstractItemView::SingleSelection);
	widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
	widget->setAlternatingRowColors(true);
	widget->verticalHeader()->hide();
	widget->horizontalHeader()->setStretchLastSection(true);
	return widget;
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_git(this)
{
	setWindowTitle(tr("TortoiseGit Qt"));
	resize(1200, 820);
	setMinimumSize(860, 620);
	buildUi();
	buildMenus();
	connect(&m_git, &tgit::GitClient::busyChanged, this, [this](bool busy)
	{
		setProperty("gitBusy", busy);
		updateControls();
		if (!busy) statusBar()->showMessage(tr("Ready · %1 changed paths").arg(m_model->rowCount()));
		if (!busy && m_refreshPending)
		{
			m_refreshPending = false;
			refresh();
		}
	});
	connect(&m_git, &tgit::GitClient::commandStarted, this, [this](const QStringList& arguments)
	{
		statusBar()->showMessage(tr("Running %1…").arg(arguments.value(0)));
	});
	connect(&m_git, &tgit::GitClient::diagnostic, this, [this](const QString& text)
	{
		m_output->appendPlainText(redact(text));
	});
	m_refreshTimer.setSingleShot(true);
	m_refreshTimer.setInterval(500);
	connect(&m_refreshTimer, &QTimer::timeout, this, &MainWindow::refresh);
	connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_refreshTimer.start(); });
	connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_refreshTimer.start(); });
	m_pollTimer.setInterval(5000);
	connect(&m_pollTimer, &QTimer::timeout, this, [this]
	{
		if (isActiveWindow() && !m_git.busy()) refresh();
	});
	m_pollTimer.start();
	if (!qApp->property("noSettings").toBool())
	{
		QSettings settings;
		restoreGeometry(settings.value("windowGeometry").toByteArray());
		const auto gitPath = settings.value("gitExecutable").toString();
		if (!gitPath.isEmpty()) m_git.setExecutable(gitPath);
	}
	updateControls();
}

void MainWindow::buildUi()
{
	auto* central = new QWidget(this);
	auto* layout = new QVBoxLayout(central);
	layout->setContentsMargins(16, 12, 16, 8);
	auto* heading = new QHBoxLayout;
	m_repositoryLabel = new QLabel(tr("Open a Git working tree to get started"), central);
	m_repositoryLabel->setTextFormat(Qt::PlainText);
	m_repositoryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	auto headingFont = m_repositoryLabel->font();
	headingFont.setPointSize(headingFont.pointSize() + 3);
	headingFont.setBold(true);
	m_repositoryLabel->setFont(headingFont);
	m_branchLabel = new QLabel(central);
	m_branchLabel->setTextFormat(Qt::PlainText);
	heading->addWidget(m_repositoryLabel, 1);
	heading->addWidget(m_branchLabel);
	layout->addLayout(heading);

	auto* workspace = new QSplitter(Qt::Horizontal, central);
	m_navigation = new QListWidget(workspace);
	m_navigation->addItems({tr("Changes"), tr("History"), tr("Branches & tags"), tr("Stashes")});
	m_navigation->setMaximumWidth(190);
	m_navigation->setMinimumWidth(140);
	m_navigation->setSpacing(7);
	m_pages = new QStackedWidget(workspace);
	workspace->setStretchFactor(1, 1);
	layout->addWidget(workspace, 1);

	auto* changes = new QWidget(m_pages);
	auto* changesLayout = new QVBoxLayout(changes);
	changesLayout->setContentsMargins(8, 0, 0, 0);
	auto* search = new QLineEdit(changes);
	search->setPlaceholderText(tr("Filter changed paths…"));
	search->setClearButtonEnabled(true);
	changesLayout->addWidget(search);
	auto* panes = new QSplitter(Qt::Vertical, changes);
	m_model = new StatusModel(this);
	m_proxy = new QSortFilterProxyModel(this);
	m_proxy->setSourceModel(m_model);
	m_proxy->setFilterKeyColumn(1);
	m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_files = new QTableView(panes);
	m_files->setObjectName("changedFiles");
	m_files->setModel(m_proxy);
	m_files->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_files->setSortingEnabled(true);
	m_files->sortByColumn(1, Qt::AscendingOrder);
	m_files->setAlternatingRowColors(true);
	m_files->verticalHeader()->hide();
	m_files->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_files->setColumnWidth(0, 150);
	m_files->setColumnWidth(2, 60);
	m_files->setColumnWidth(3, 70);
	m_diffTabs = new QTabWidget(panes);
	m_diffTabs->addTab(preview(m_diffTabs), tr("Working tree diff"));
	m_diffTabs->addTab(preview(m_diffTabs), tr("Staged diff"));
	m_diff = qobject_cast<QPlainTextEdit*>(m_diffTabs->widget(0));
	panes->setSizes({240, 250});
	changesLayout->addWidget(panes, 1);
	auto* fileActions = new QHBoxLayout;
	auto addButton = [this, fileActions, changes](const QString& label, const QString& name, const std::function<void()>& callback)
	{
		auto* button = new QPushButton(label, changes);
		button->setObjectName(name);
		connect(button, &QPushButton::clicked, this, callback);
		fileActions->addWidget(button);
		m_mutationButtons.append(button);
	};
	addButton(tr("Stage selected"), "stageSelected", [this] { stage(false); });
	addButton(tr("Stage all"), "stageAll", [this] { stage(true); });
	addButton(tr("Unstage selected"), "unstageSelected", [this] { unstage(); });
	addButton(tr("Discard unstaged…"), "discardSelected", [this] { discard(); });
	fileActions->addStretch();
	changesLayout->addLayout(fileActions);
	m_summary = new QLabel(changes);
	changesLayout->addWidget(m_summary);
	m_commitMessage = new QPlainTextEdit(changes);
	m_commitMessage->setObjectName("commitMessage");
	m_commitMessage->setPlaceholderText(tr("Commit message — commits the current index only"));
	m_commitMessage->setMaximumHeight(85);
	changesLayout->addWidget(m_commitMessage);
	auto* commitRow = new QHBoxLayout;
	m_amend = new QCheckBox(tr("Amend last commit"), changes);
	commitRow->addWidget(m_amend);
	commitRow->addStretch();
	m_commitButton = new QPushButton(tr("Commit staged changes"), changes);
	m_commitButton->setObjectName("commitStaged");
	m_commitButton->setDefault(true);
	commitRow->addWidget(m_commitButton);
	changesLayout->addLayout(commitRow);
	m_pages->addWidget(changes);

	auto* historyPane = new QSplitter(Qt::Vertical, m_pages);
	m_history = table({tr("Commit"), tr("Author"), tr("Date"), tr("Subject")}, historyPane);
	m_history->setColumnWidth(0, 100);
	m_history->setColumnWidth(1, 150);
	m_history->setColumnWidth(2, 160);
	m_commitDetails = preview(historyPane);
	historyPane->setSizes({300, 300});
	m_pages->addWidget(historyPane);
	m_refs = table({tr("Reference"), tr("Object"), tr("Subject")}, m_pages);
	m_refs->setColumnWidth(0, 260);
	m_pages->addWidget(m_refs);
	m_stashes = table({tr("Stash"), tr("Subject")}, m_pages);
	m_pages->addWidget(m_stashes);
	m_navigation->setCurrentRow(0);
	connect(m_navigation, &QListWidget::currentRowChanged, this, [this](int page)
	{
		m_pages->setCurrentIndex(page);
		if (m_root.isEmpty()) return;
		if (page == 1) loadHistory();
		else if (page == 2) loadReferences();
		else if (page == 3) loadStashes();
	});
	connect(search, &QLineEdit::textChanged, m_proxy, &QSortFilterProxyModel::setFilterFixedString);
	connect(m_files->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { showDiff(); updateControls(); });
	connect(m_diffTabs, &QTabWidget::currentChanged, this, &MainWindow::showDiff);
	connect(m_commitMessage, &QPlainTextEdit::textChanged, this, &MainWindow::updateControls);
	connect(m_amend, &QCheckBox::toggled, this, &MainWindow::updateControls);
	connect(m_commitButton, &QPushButton::clicked, this, &MainWindow::commit);
	connect(m_history, &QTableWidget::itemSelectionChanged, this, [this]
	{
		const auto row = m_history->currentRow();
		if (row >= 0 && m_history->item(row, 0)) showPatch(m_history->item(row, 0)->data(Qt::UserRole).toString());
	});
	connect(m_refs, &QTableWidget::cellDoubleClicked, this, [this](int row, int)
	{
		if (m_refs->item(row, 1)) showResult(tr("Reference"), {"show", "--stat", m_refs->item(row, 1)->data(Qt::UserRole).toString(), "--"});
	});
	m_output = preview(central);
	m_output->setObjectName("operationOutput");
	m_output->setMaximumHeight(85);
	m_output->document()->setMaximumBlockCount(300);
	m_output->setPlaceholderText(tr("Operation results appear here"));
	layout->addWidget(m_output);
	setCentralWidget(central);
}

QAction* MainWindow::addRepositoryAction(QMenu* menu, const QString& text, const std::function<void()>& action)
{
	auto* item = menu->addAction(text, this, action);
	m_repositoryActions.append(item);
	return item;
}

void MainWindow::buildMenus()
{
	auto* repository = menuBar()->addMenu(tr("&Repository"));
	m_openAction = repository->addAction(tr("Open…"), QKeySequence::Open, this, [this]
	{
		const auto path = QFileDialog::getExistingDirectory(this, tr("Open Git working tree"), m_root);
		if (!path.isEmpty()) openRepository(path);
	});
	m_cloneAction = repository->addAction(tr("Clone…"), this, &MainWindow::cloneRepository);
	m_initAction = repository->addAction(tr("Create repository…"), this, &MainWindow::initRepository);
	auto* refreshAction = addRepositoryAction(repository, tr("Refresh"), [this] { refresh(); });
	refreshAction->setShortcut(QKeySequence::Refresh);
	addRepositoryAction(repository, tr("Show in Finder"), [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_root)); });
	repository->addSeparator();
	repository->addAction(tr("Git executable…"), this, &MainWindow::configureGit);
	repository->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);

	auto* remote = menuBar()->addMenu(tr("&Remote"));
	auto* fetchAction = addRepositoryAction(remote, tr("Fetch"), [this] { runMutation(tr("Fetch"), {"fetch", "--all"}); });
	auto* pullAction = addRepositoryAction(remote, tr("Pull (fast-forward only)"), [this]
	{
		if (confirm(tr("Pull"), tr("Fetch and fast-forward the current branch from its configured upstream?")))
			runMutation(tr("Pull"), {"pull", "--ff-only"});
	});
	auto* pushAction = addRepositoryAction(remote, tr("Push…"), [this] { push(); });
	addRepositoryAction(remote, tr("Manage remotes…"), [this] { configureRemote(); });
	addRepositoryAction(remote, tr("Remote details"), [this] { showResult(tr("Remotes"), {"remote", "-v"}); });

	auto* refs = menuBar()->addMenu(tr("&Branch"));
	addRepositoryAction(refs, tr("Create branch…"), [this] { branchOrTag(false); });
	addRepositoryAction(refs, tr("Create tag…"), [this] { branchOrTag(true); });
	addRepositoryAction(refs, tr("Switch branch…"), [this] { checkout(); });
	addRepositoryAction(refs, tr("Merge…"), [this]
	{
		operation(tr("Merge"), {"merge"}, tr("Branch or commit"), tr("Merge into the current branch. Conflicts remain available for editing."), {"--"});
	});
	addRepositoryAction(refs, tr("Rebase…"), [this]
	{
		operation(tr("Rebase"), {"rebase"}, tr("Upstream"), tr("Replay current branch commits onto the selected upstream."));
	});
	addRepositoryAction(refs, tr("Cherry-pick…"), [this]
	{
		operation(tr("Cherry-pick"), {"cherry-pick"}, tr("Commit"), tr("Apply the selected commit to this branch."));
	});
	addRepositoryAction(refs, tr("Revert commit…"), [this]
	{
		operation(tr("Revert commit"), {"revert", "--no-edit"}, tr("Commit"), tr("Create a new commit that reverses the selected commit."));
	});
	refs->addSeparator();
	for (const auto& command : QStringList{"merge", "rebase", "cherry-pick", "revert"})
	{
		auto* submenu = refs->addMenu(command);
		for (const auto& action : QStringList{"continue", "abort"})
			addRepositoryAction(submenu, action, [this, command, action]
			{
				if (confirm(command, tr("%1 the current %2 operation?").arg(action, command)))
					runMutation(command, {command, "--" + action});
			});
	}
	addRepositoryAction(refs, tr("Reflog"), [this] { showResult(tr("Reflog"), {"reflog", "-100", "--date=iso"}); });

	auto* changes = menuBar()->addMenu(tr("&Changes"));
	addRepositoryAction(changes, tr("Stage selected"), [this] { stage(false); });
	addRepositoryAction(changes, tr("Unstage selected"), [this] { unstage(); });
	addRepositoryAction(changes, tr("Edit conflicted file…"), [this] { editConflict(); });
	addRepositoryAction(changes, tr("Blame selected file"), [this] { showBlame(); });
	addRepositoryAction(changes, tr("Save staged patch…"), [this] { savePatch(); });
	addRepositoryAction(changes, tr("Apply patch…"), [this] { applyPatch(); });
	changes->addSeparator();
	addRepositoryAction(changes, tr("Stash changes…"), [this]
	{
		operation(tr("Stash"), {"stash", "push", "--include-untracked", "-m"}, tr("Message"),
			tr("Save tracked and untracked changes, then restore the working tree to the index/HEAD state."));
	});
	for (const auto& command : QStringList{"apply", "pop", "drop"})
		addRepositoryAction(changes, tr("Stash %1…").arg(command), [this, command]
		{
			operation(tr("Stash %1").arg(command), {"stash", command}, tr("Stash reference (for example stash@{0})"),
				command == "drop" ? tr("Permanently remove this stash reference.") : tr("Restore the selected stash. Conflicts may require resolution."));
		});

	auto* tools = menuBar()->addMenu(tr("&Tools"));
	addRepositoryAction(tools, tr("Submodule update"), [this]
	{
		if (confirm(tr("Submodules"), tr("Initialize and update submodules recursively?")))
			runMutation(tr("Submodules"), {"submodule", "update", "--init", "--recursive"});
	});
	addRepositoryAction(tools, tr("Submodule status"), [this] { showResult(tr("Submodules"), {"submodule", "status", "--recursive"}); });
	addRepositoryAction(tools, tr("Worktrees"), [this] { showResult(tr("Worktrees"), {"worktree", "list", "--porcelain"}); });
	addRepositoryAction(tools, tr("Create worktree…"), [this]
	{
		OperationDialog dialog(tr("Create worktree"), tr("Create a linked worktree for an existing branch."), this);
		dialog.addText("path", tr("New directory"));
		dialog.addText("branch", tr("Existing branch"));
		if (dialog.exec() == QDialog::Accepted && !dialog.value("path").isEmpty() && !dialog.value("branch").startsWith('-'))
			runMutation(tr("Create worktree"), {"worktree", "add", "--", QDir::cleanPath(QDir(m_root).absoluteFilePath(dialog.value("path"))), dialog.value("branch")});
	});
	addRepositoryAction(tools, tr("Repository check"), [this] { showResult(tr("Repository check"), {"fsck", "--no-reflogs"}); });
	tools->addAction(tr("Compare images…"), this, &MainWindow::openImageDiff);
	auto* help = menuBar()->addMenu(tr("&Help"));
	help->addAction(tr("About TortoiseGit Qt"), this, [this]
	{
		notice(this, tr("TortoiseGit Qt 0.1"), tr("Native Qt Widgets preview for macOS.\n\nGit working trees, staging, commits, history, branches, remotes and recovery commands.\n\nFinder integration and full Windows feature parity are still in development.\nGPL-2.0-or-later; see the repository license and dependency notices."));
	});
	help->addAction(tr("About Qt"), qApp, &QApplication::aboutQt);

	auto* toolbar = addToolBar(tr("Repository"));
	toolbar->setMovable(false);
	toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
	toolbar->addAction(m_openAction);
	toolbar->addAction(m_cloneAction);
	toolbar->addSeparator();
	toolbar->addAction(refreshAction);
	toolbar->addAction(fetchAction);
	toolbar->addAction(pullAction);
	toolbar->addAction(pushAction);
	toolbar->addSeparator();
	m_cancelAction = toolbar->addAction(tr("Cancel operation"), this, [this] { m_git.cancel(); });
}

void MainWindow::openRepository(const QString& path)
{
	if (m_git.busy()) return;
	const QFileInfo info(path);
	const auto directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
	const auto generation = ++m_generation;
	setProperty("repositoryReady", false);
	m_git.execute(directory, {"rev-parse", "--show-toplevel"}, [this, generation](const tgit::GitResult& result)
	{
		if (generation != m_generation) return;
		if (!result.ok()) { reportError(tr("Open working tree"), result); return; }
		m_root = oneLine(result.output);
		m_repositoryLabel->setText(QFileInfo(m_root).fileName());
		m_repositoryLabel->setToolTip(m_root);
		setWindowTitle(QFileInfo(m_root).fileName() + " — TortoiseGit Qt");
		m_model->setStatus({});
		m_history->setRowCount(0);
		m_refs->setRowCount(0);
		m_stashes->setRowCount(0);
		m_commitMessage->clear();
		m_amend->setChecked(false);
		if (!qApp->property("noSettings").toBool()) QSettings().setValue("lastRepository", m_root);
		updateWatchers();
		refresh();
	});
}

void MainWindow::refresh()
{
	if (m_root.isEmpty()) return;
	if (m_git.busy()) { m_refreshPending = true; return; }
	const auto generation = m_generation;
	const auto selected = selectedPaths(false);
	m_git.execute(m_root, {"status", "--porcelain=v2", "--branch", "-z", "--untracked-files=all"},
		[this, generation, selected](const tgit::GitResult& result)
	{
		if (generation != m_generation || result.cancelled) return;
		if (!result.ok()) { reportError(tr("Status"), result); return; }
		auto parsed = tgit::parseStatus(std::string_view(result.output.constData(), result.output.size()));
		if (!parsed) { notice(this, tr("Status"), decode(parsed.error), QMessageBox::Critical); return; }
		const auto& status = parsed.status;
		m_branchLabel->setText(tr("%1  ↑%2 ↓%3").arg(decode(status.branch)).arg(status.ahead).arg(status.behind));
		int staged = 0, unstaged = 0, conflicts = 0;
		for (const auto& entry : status.entries)
		{
			staged += entry.staged(); unstaged += entry.unstaged(); conflicts += entry.conflicted;
		}
		m_summary->setText(tr("%1 staged · %2 unstaged · %3 conflicts").arg(staged).arg(unstaged).arg(conflicts));
		m_model->setStatus(std::move(parsed.status));
		for (int row = 0; row < m_model->rowCount(); ++row)
			if (selected.contains(decode(m_model->entry(row).path)))
				m_files->selectionModel()->select(m_proxy->mapFromSource(m_model->index(row, 0)), QItemSelectionModel::Select | QItemSelectionModel::Rows);
		if (m_files->selectionModel()->selectedRows().isEmpty() && m_proxy->rowCount())
			m_files->selectRow(0);
		showDiff();
		setProperty("repositoryReady", true);
		statusBar()->showMessage(tr("%1 changed paths").arg(m_model->rowCount()), 5000);
		updateWatchers();
		updateControls();
	});
	if (m_navigation->currentRow() == 1) loadHistory();
	else if (m_navigation->currentRow() == 2) loadReferences();
	else if (m_navigation->currentRow() == 3) loadStashes();
}

std::vector<tgit::StatusEntry> MainWindow::selectedEntries() const
{
	std::vector<tgit::StatusEntry> entries;
	for (const auto& index : m_files->selectionModel()->selectedRows())
	{
		const auto source = m_proxy->mapToSource(index);
		if (source.isValid()) entries.push_back(m_model->entry(source.row()));
	}
	return entries;
}

QStringList MainWindow::selectedPaths(bool includeOriginal) const
{
	QStringList paths;
	for (const auto& entry : selectedEntries())
	{
		if (!StatusModel::usablePath(entry.path) || !StatusModel::usablePath(entry.originalPath)) return {};
		paths.append(decode(entry.path));
		if (includeOriginal && !entry.originalPath.empty()) paths.append(decode(entry.originalPath));
	}
	paths.removeDuplicates();
	return paths;
}

void MainWindow::showDiff()
{
	m_diff = qobject_cast<QPlainTextEdit*>(m_diffTabs->currentWidget());
	const auto entries = selectedEntries();
	const auto generation = ++m_diffGeneration;
	const QPointer<QPlainTextEdit> target(m_diff);
	if (entries.empty() || m_root.isEmpty()) { target->clear(); return; }
	const auto paths = selectedPaths();
	if (paths.isEmpty()) { target->setPlainText(tr("This filename cannot be represented losslessly as UTF-8. Use Git in Terminal.")); return; }
	QStringList arguments{"diff", "--no-ext-diff", "--no-textconv"};
	const bool staged = m_diffTabs->currentIndex() == 1;
	if (staged) arguments << "--cached";
	if (entries.size() == 1 && entries.front().untracked && !staged)
		arguments << "--no-index" << "--" << "/dev/null" << paths.front();
	else arguments << "--" << paths;
	target->setPlainText(tr("Loading diff…"));
	m_git.execute(m_root, arguments, [this, generation, target](const tgit::GitResult& result)
	{
		if (!target || generation != m_diffGeneration || result.cancelled) return;
		if (result.exitCode != 0 && result.exitCode != 1) target->setPlainText(result.message());
		else target->setPlainText(result.output.isEmpty() ? tr("No changes in this view.") : QString::fromUtf8(result.output));
	});
}

void MainWindow::stage(bool all)
{
	if (m_git.busy() || m_root.isEmpty()) return;
	if (all)
	{
		// Refuse paths that cannot be passed back to Git losslessly, even for add -A.
		for (const auto& entry : m_model->status().entries)
			if (!StatusModel::usablePath(entry.path)) { notice(this, tr("Stage"), tr("Rename non-UTF-8 paths in Terminal before staging all.")); return; }
		runMutation(tr("Stage all"), {"add", "--all", "--", "."});
		return;
	}
	QStringList paths;
	bool conflict = false;
	for (const auto& entry : selectedEntries())
	{
		if (!StatusModel::usablePath(entry.path) || !StatusModel::usablePath(entry.originalPath)) return;
		paths << decode(entry.path);
		// An index rename's original name is already removed from the index.
		if (!entry.originalPath.empty() && entry.index == '.') paths << decode(entry.originalPath);
		conflict |= entry.conflicted;
	}
	if (conflict && !confirm(tr("Mark resolved"), tr("Stage the selected conflict results? Ensure you have resolved the file contents first."))) return;
	if (!paths.isEmpty()) runMutation(tr("Stage selected"), QStringList{"add", "--all", "--"} + paths);
}

void MainWindow::unstage()
{
	if (m_git.busy()) return;
	QStringList paths;
	for (const auto& entry : selectedEntries())
	{
		if (!entry.staged()) continue;
		if (!StatusModel::usablePath(entry.path) || !StatusModel::usablePath(entry.originalPath)) return;
		paths << decode(entry.path);
		if (!entry.originalPath.empty()) paths << decode(entry.originalPath);
	}
	if (paths.isEmpty()) return;
	const QStringList prefix = m_model->status().unborn ? QStringList{"rm", "--cached", "--force", "--ignore-unmatch", "--"}
		: QStringList{"restore", "--staged", "--"};
	runMutation(tr("Unstage"), prefix + paths);
}

void MainWindow::discard()
{
	if (m_git.busy()) return;
	const auto paths = selectedPaths(false);
	if (paths.isEmpty()) return;
	for (const auto& entry : selectedEntries())
		if (entry.untracked || entry.conflicted)
		{
			notice(this, tr("Discard"), tr("Select tracked, non-conflicted files. Untracked files are kept and conflicts need explicit resolution."));
			return;
		}
	if (confirm(tr("Discard unstaged changes"), tr("Replace the working files below with their index versions? Unstaged edits will be lost.\n\n%1").arg(paths.join('\n'))))
		runMutation(tr("Discard"), QStringList{"restore", "--worktree", "--"} + paths);
}

void MainWindow::commit()
{
	if (m_git.busy() || m_root.isEmpty()) return;
	const auto message = m_commitMessage->toPlainText();
	if (message.trimmed().isEmpty()) return;
	if (m_amend->isChecked() && !confirm(tr("Amend"), tr("Replace the last commit using the current index and this message? Its commit ID will change."))) return;
	QStringList arguments{"commit", "--file=-"};
	if (m_amend->isChecked()) arguments << "--amend";
	m_git.execute(m_root, {"diff", "--cached", "--quiet", "--exit-code"}, [this, message, arguments](const tgit::GitResult& check)
	{
		if (check.cancelled) return;
		if (check.exitCode != 0 && check.exitCode != 1)
		{
			reportError(tr("Commit"), check);
			return;
		}
		if (check.exitCode == 0 && !arguments.contains("--amend"))
		{
			if (check.exitCode == 0) notice(this, tr("Commit"), tr("No staged changes. Stage files before committing."));
			else reportError(tr("Commit"), check);
			return;
		}
		m_git.execute(m_root, arguments, [this](const tgit::GitResult& result)
		{
			if (result.ok())
			{
				m_commitMessage->clear();
				m_amend->setChecked(false);
				m_output->appendPlainText(redact(QString::fromUtf8(result.output)));
			}
			else reportError(tr("Commit"), result);
			refresh();
		}, message.toUtf8() + '\n');
	});
}

void MainWindow::runMutation(const QString& title, const QStringList& arguments, const QByteArray& input)
{
	if (m_root.isEmpty()) return;
	const auto generation = m_generation;
	m_output->appendPlainText(title + "…");
	m_git.execute(m_root, arguments, [this, title, generation](const tgit::GitResult& result)
	{
		if (generation != m_generation) return;
		if (!result.ok()) reportError(title, result);
		else
		{
			m_output->appendPlainText(redact(QString::fromUtf8(result.output)));
			statusBar()->showMessage(title + tr(" completed"), 5000);
		}
		refresh();
	}, input);
}

void MainWindow::loadHistory()
{
	const auto generation = m_generation;
	m_git.execute(m_root, {"log", "-200", "-z", "--format=%H%x00%h%x00%an%x00%aI%x00%s"}, [this, generation](const tgit::GitResult& result)
	{
		if (generation != m_generation || result.cancelled) return;
		m_history->setRowCount(0);
		if (!result.ok()) { m_commitDetails->setPlainText(result.message()); return; }
		const auto fields = result.output.split('\0');
		for (qsizetype offset = 0; offset + 4 < fields.size(); offset += 5)
		{
			const int row = m_history->rowCount(); m_history->insertRow(row);
			const QStringList values{QString::fromUtf8(fields[offset + 1]), QString::fromUtf8(fields[offset + 2]),
				QString::fromUtf8(fields[offset + 3]).left(16).replace('T', ' '), QString::fromUtf8(fields[offset + 4])};
			for (int col = 0; col < values.size(); ++col) m_history->setItem(row, col, new QTableWidgetItem(values[col]));
			m_history->item(row, 0)->setData(Qt::UserRole, QString::fromLatin1(fields[offset]));
		}
		if (m_history->rowCount()) m_history->selectRow(0);
	});
}

void MainWindow::showPatch(const QString& hash)
{
	const auto generation = m_generation;
	m_git.execute(m_root, {"show", "--no-ext-diff", "--no-textconv", "--format=fuller", "--stat", "--patch", hash, "--"},
		[this, hash, generation](const tgit::GitResult& result)
	{
		const int row = m_history->currentRow();
		if (generation != m_generation || row < 0 || !m_history->item(row, 0) || m_history->item(row, 0)->data(Qt::UserRole).toString() != hash) return;
		m_commitDetails->setPlainText(result.ok() ? QString::fromUtf8(result.output) : result.message());
	});
}

void MainWindow::loadReferences()
{
	const auto generation = m_generation;
	m_git.execute(m_root, {"for-each-ref", "--format=%(refname)%00%(objectname)%00%(subject)%00"}, [this, generation](const tgit::GitResult& result)
	{
		if (generation != m_generation || !result.ok()) return;
		m_refs->setRowCount(0);
		for (const auto& line : result.output.split('\n'))
		{
			const auto fields = line.split('\0');
			if (fields.size() < 3) continue;
			const int row = m_refs->rowCount(); m_refs->insertRow(row);
			for (int col = 0; col < 3; ++col)
				m_refs->setItem(row, col, new QTableWidgetItem(QString::fromUtf8(col == 1 ? fields[col].left(12) : fields[col])));
			m_refs->item(row, 1)->setData(Qt::UserRole, QString::fromLatin1(fields[1]));
		}
	});
}

void MainWindow::loadStashes()
{
	const auto generation = m_generation;
	m_git.execute(m_root, {"stash", "list", "-z", "--format=%gd%x00%gs"}, [this, generation](const tgit::GitResult& result)
	{
		if (generation != m_generation || !result.ok()) return;
		m_stashes->setRowCount(0);
		const auto fields = result.output.split('\0');
		for (qsizetype offset = 0; offset + 1 < fields.size(); offset += 2)
		{
			const int row = m_stashes->rowCount(); m_stashes->insertRow(row);
			m_stashes->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(fields[offset])));
			m_stashes->setItem(row, 1, new QTableWidgetItem(QString::fromUtf8(fields[offset + 1])));
		}
	});
}

void MainWindow::showResult(const QString& title, const QStringList& arguments)
{
	auto* dialog = new QDialog(this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(title);
	dialog->resize(900, 640);
	auto* layout = new QVBoxLayout(dialog);
	const QPointer<QPlainTextEdit> text(preview(dialog));
	text->setPlainText(tr("Loading…"));
	layout->addWidget(text);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	layout->addWidget(buttons);
	dialog->show();
	m_git.execute(m_root, arguments, [text](const tgit::GitResult& result)
	{
		if (text) text->setPlainText(redact(result.ok() ? QString::fromUtf8(result.output) : result.message()));
	});
}

bool MainWindow::confirm(const QString& title, const QString& details)
{
	QMessageBox dialog(QMessageBox::Question, title, details, QMessageBox::Ok | QMessageBox::Cancel, this);
	dialog.setTextFormat(Qt::PlainText);
	dialog.setDefaultButton(QMessageBox::Cancel);
	return dialog.exec() == QMessageBox::Ok;
}

void MainWindow::reportError(const QString& title, const tgit::GitResult& result)
{
	m_output->appendPlainText(redact(title + ": " + result.message()));
	if (!result.cancelled) notice(this, title, result.message(), QMessageBox::Critical);
}

void MainWindow::updateControls()
{
	const bool ready = !m_root.isEmpty() && !m_git.busy();
	for (auto* action : m_repositoryActions) action->setEnabled(ready);
	if (m_cancelAction) m_cancelAction->setEnabled(m_git.busy());
	if (m_openAction) m_openAction->setEnabled(!m_git.busy());
	if (m_cloneAction) m_cloneAction->setEnabled(!m_git.busy());
	if (m_initAction) m_initAction->setEnabled(!m_git.busy());
	const auto selected = selectedEntries();
	bool staged = false, conflict = false;
	for (const auto& entry : m_model->status().entries) { staged |= entry.staged(); conflict |= entry.conflicted; }
	for (auto* button : m_mutationButtons)
		button->setEnabled(ready && (button->objectName() == "stageAll" ? m_model->rowCount() > 0 : !selected.empty() && !selectedPaths().isEmpty()));
	m_commitButton->setEnabled(ready && (staged || (m_amend->isChecked() && !m_model->status().unborn)) && !conflict && !m_commitMessage->toPlainText().trimmed().isEmpty());
	m_amend->setEnabled(ready && !m_model->status().unborn);
	m_commitMessage->setEnabled(ready);
}

void MainWindow::updateWatchers()
{
	if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
	if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
	if (m_root.isEmpty()) return;
	QStringList paths{m_root};
	const auto dotGit = QDir(m_root).filePath(".git");
	QString gitDirectory = dotGit;
	if (QFileInfo(dotGit).isFile())
	{
		QFile file(dotGit);
		if (file.open(QIODevice::ReadOnly))
		{
			auto contents = file.readAll();
			if (contents.startsWith("gitdir: "))
				gitDirectory = QDir(m_root).absoluteFilePath(oneLine(contents.mid(8)));
		}
		paths << dotGit;
	}
	if (QFileInfo(gitDirectory).isDir())
	{
		paths << gitDirectory;
		for (const auto& file : QStringList{"HEAD", "index", "packed-refs"})
			if (QFileInfo::exists(QDir(gitDirectory).filePath(file))) paths << QDir(gitDirectory).filePath(file);
	}
	m_watcher.addPaths(paths);
}

void MainWindow::cloneRepository()
{
	OperationDialog dialog(tr("Clone repository"), tr("Clone a repository into a new directory. Git credentials will be requested when needed."), this);
	dialog.addText("remote", tr("Repository URL or local path"));
	dialog.addText("destination", tr("New directory (absolute path)"), QDir::homePath() + "/Projects/");
	if (dialog.exec() != QDialog::Accepted || dialog.value("remote").isEmpty() || dialog.value("destination").isEmpty()) return;
	const auto destination = QDir::cleanPath(dialog.value("destination"));
	if (!QDir::isAbsolutePath(destination)) { notice(this, tr("Clone"), tr("Choose an absolute destination path.")); return; }
	m_git.execute(QDir::homePath(), {"clone", "--", dialog.value("remote"), destination}, [this, destination](const tgit::GitResult& result)
	{
		if (!result.ok()) reportError(tr("Clone"), result);
		else openRepository(destination);
	});
}

void MainWindow::initRepository()
{
	const auto path = QFileDialog::getExistingDirectory(this, tr("Create Git repository in directory"));
	if (path.isEmpty() || !confirm(tr("Create repository"), tr("Initialize a Git repository in:\n%1").arg(path))) return;
	m_git.execute(path, {"init"}, [this, path](const tgit::GitResult& result)
	{
		if (!result.ok()) reportError(tr("Create repository"), result);
		else openRepository(path);
	});
}

void MainWindow::push()
{
	m_git.execute(m_root, {"remote"}, [this](const tgit::GitResult& result)
	{
		if (!result.ok()) { reportError(tr("Push"), result); return; }
		const auto remotes = QString::fromUtf8(result.output).split('\n', Qt::SkipEmptyParts);
		if (remotes.isEmpty()) { notice(this, tr("Push"), tr("Add a remote using Remote → Manage remotes.")); return; }
		OperationDialog dialog(tr("Push branch"), tr("Push the current HEAD to the selected remote branch and set its upstream. This does not force push."), this);
		dialog.addChoice("remote", tr("Remote"), remotes);
		dialog.addText("branch", tr("Destination branch"), decode(m_model->status().branch));
		if (dialog.exec() != QDialog::Accepted || dialog.value("branch").isEmpty()) return;
		const auto branch = dialog.value("branch");
		const auto remote = dialog.value("remote");
		m_git.execute(m_root, {"check-ref-format", "--branch", branch}, [this, branch, remote](const tgit::GitResult& check)
		{
			if (!check.ok()) { reportError(tr("Branch name"), check); return; }
			runMutation(tr("Push"), {"push", "--porcelain", "--set-upstream", "--", remote, "HEAD:refs/heads/" + branch});
		});
	});
}

void MainWindow::branchOrTag(bool tag)
{
	OperationDialog dialog(tag ? tr("Create tag") : tr("Create branch"), tr("Create a reference at the selected starting commit."), this);
	dialog.addText("name", tr("Name"));
	dialog.addText("start", tr("Starting commit"), "HEAD");
	if (dialog.exec() != QDialog::Accepted || dialog.value("name").isEmpty() || dialog.value("start").startsWith('-')) return;
	const auto name = dialog.value("name");
	const auto start = dialog.value("start");
	const auto reference = (tag ? "refs/tags/" : "refs/heads/") + name;
	m_git.execute(m_root, {"check-ref-format", reference}, [this, name, start, tag](const tgit::GitResult& result)
	{
		if (!result.ok()) { reportError(tr("Reference name"), result); return; }
		runMutation(tag ? tr("Create tag") : tr("Create branch"), {tag ? "tag" : "branch", "--", name, start});
	});
}

void MainWindow::checkout()
{
	m_git.execute(m_root, {"for-each-ref", "--format=%(refname:short)", "refs/heads/"}, [this](const tgit::GitResult& result)
	{
		if (!result.ok()) { reportError(tr("Switch branch"), result); return; }
		bool accepted = false;
		const auto branch = QInputDialog::getItem(this, tr("Switch branch"), tr("Local branch"), QString::fromUtf8(result.output).split('\n', Qt::SkipEmptyParts), 0, false, &accepted);
		if (accepted && !branch.isEmpty()) runMutation(tr("Switch branch"), {"switch", "--", branch});
	});
}

void MainWindow::operation(const QString& title, const QStringList& prefix, const QString& field, const QString& description, const QStringList& suffix)
{
	OperationDialog dialog(title, description, this);
	dialog.addText("value", field);
	if (dialog.exec() != QDialog::Accepted) return;
	const auto value = dialog.value("value");
	if (value.isEmpty() || value.startsWith('-')) { notice(this, title, tr("Enter a value that does not start with '-'.")); return; }
	runMutation(title, prefix + QStringList{value} + suffix);
}

void MainWindow::showBlame()
{
	const auto paths = selectedPaths(false);
	if (paths.size() != 1) { notice(this, tr("Blame"), tr("Select one tracked file in Changes.")); return; }
	showResult(tr("Blame — %1").arg(paths.front()), {"blame", "--date=short", "--", paths.front()});
}

void MainWindow::savePatch()
{
	const auto path = QFileDialog::getSaveFileName(this, tr("Save staged changes as patch"), "changes.patch", tr("Git patch (*.patch)"));
	if (path.isEmpty()) return;
	m_git.execute(m_root, {"diff", "--cached", "--binary", "--no-ext-diff", "--no-textconv"}, [this, path](const tgit::GitResult& result)
	{
		if (!result.ok()) { reportError(tr("Save patch"), result); return; }
		QSaveFile file(path);
		if (!file.open(QIODevice::WriteOnly) || file.write(result.output) != result.output.size() || !file.commit())
			notice(this, tr("Save patch"), file.errorString(), QMessageBox::Critical);
	});
}

void MainWindow::applyPatch()
{
	const auto path = QFileDialog::getOpenFileName(this, tr("Apply patch"), {}, tr("Patches (*.patch *.diff);;All files (*)"));
	if (path.isEmpty() || !confirm(tr("Apply patch"), tr("Apply this patch to the working tree?\n%1").arg(path))) return;
	m_git.execute(m_root, {"apply", "--check", "--", path}, [this, path](const tgit::GitResult& result)
	{
		if (!result.ok()) { reportError(tr("Check patch"), result); return; }
		runMutation(tr("Apply patch"), {"apply", "--", path});
	});
}

void MainWindow::configureGit()
{
	if (m_git.busy()) return;
	const auto path = QFileDialog::getOpenFileName(this, tr("Select Git executable"), m_git.executable());
	if (path.isEmpty()) return;
	if (!QFileInfo(path).isExecutable()) { notice(this, tr("Git executable"), tr("The selected file is not executable.")); return; }
	m_git.setExecutable(path);
	if (!qApp->property("noSettings").toBool()) QSettings().setValue("gitExecutable", path);
	if (!m_root.isEmpty()) refresh();
}

void MainWindow::configureRemote()
{
	OperationDialog dialog(tr("Manage remote"), tr("Add a remote or change an existing remote URL."), this);
	dialog.addChoice("action", tr("Action"), {"add", "set-url"});
	dialog.addText("name", tr("Remote name"), "origin");
	dialog.addText("url", tr("URL or path"));
	if (dialog.exec() != QDialog::Accepted || dialog.value("name").isEmpty() || dialog.value("url").isEmpty()) return;
	runMutation(tr("Manage remote"), {"remote", dialog.value("action"), "--", dialog.value("name"), dialog.value("url")});
}

void MainWindow::editConflict()
{
	const auto entries = selectedEntries();
	if (entries.size() != 1 || !entries.front().conflicted || !StatusModel::usablePath(entries.front().path))
	{
		notice(this, tr("Conflict editor"), tr("Select one conflicted text file in Changes.")); return;
	}
	const auto path = QDir(m_root).filePath(decode(entries.front().path));
	QFile source(path);
	if (QFileInfo(path).isSymLink() || !source.open(QIODevice::ReadOnly))
	{
		notice(this, tr("Conflict editor"), tr("This conflict is not an editable regular file. Resolve its type/deletion in Terminal.")); return;
	}
	if (source.size() > 8 * 1024 * 1024) { notice(this, tr("Conflict editor"), tr("Use an external tool for files larger than 8 MiB.")); return; }
	const auto original = source.readAll(); source.close();
	if (original.contains('\0') || QString::fromUtf8(original).toUtf8() != original)
	{
		notice(this, tr("Conflict editor"), tr("Use an external tool for binary or non-UTF-8 conflicts.")); return;
	}
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Resolve conflict — %1").arg(decode(entries.front().path)));
	dialog.resize(1000, 700);
	auto* layout = new QVBoxLayout(&dialog);
	const QString explanation = tr("Edit the result, remove conflict markers, then save. Saving marks this file as resolved in the index.");
	layout->addWidget(new QLabel(explanation, &dialog));
	auto* editor = new QPlainTextEdit(&dialog);
	editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	const bool bom = original.startsWith("\xef\xbb\xbf");
	editor->setPlainText(QString::fromUtf8(bom ? original.mid(3) : original));
	layout->addWidget(editor);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted) return;
	const auto text = editor->toPlainText();
	if (QRegularExpression("(?m)^(<<<<<<< |=======|>>>>>>> )").match(text).hasMatch()
		&& !confirm(tr("Conflict markers"), tr("Conflict markers remain. Save and stage anyway?"))) return;
	QFile latest(path);
	if (!latest.open(QIODevice::ReadOnly) || latest.readAll() != original)
	{
		notice(this, tr("Conflict editor"), tr("The file changed outside this window. Reopen it before saving.")); return;
	}
	latest.close();
	auto bytes = text.toUtf8();
	if (original.contains("\r\n")) bytes.replace("\n", "\r\n");
	if (bom) bytes.prepend("\xef\xbb\xbf");
	QSaveFile target(path);
	if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() || !target.commit())
	{
		notice(this, tr("Conflict editor"), target.errorString(), QMessageBox::Critical); return;
	}
	runMutation(tr("Resolve conflict"), {"add", "--", decode(entries.front().path)});
}

void MainWindow::openImageDiff()
{
	const auto left = QFileDialog::getOpenFileName(this, tr("First image"));
	if (left.isEmpty()) return;
	const auto right = QFileDialog::getOpenFileName(this, tr("Second image"));
	if (right.isEmpty()) return;
	QPixmap a(left), b(right);
	if (a.isNull() || b.isNull()) { notice(this, tr("Image comparison"), tr("Both files must be supported images.")); return; }
	auto* dialog = new QDialog(this);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(tr("Image comparison"));
	dialog->resize(1000, 650);
	auto* layout = new QVBoxLayout(dialog);
	auto* panes = new QSplitter(Qt::Horizontal, dialog);
	QList<QGraphicsView*> views;
	for (const auto& pixmap : {a, b})
	{
		auto* scene = new QGraphicsScene(dialog); scene->addPixmap(pixmap);
		auto* view = new QGraphicsView(scene, panes);
		view->setDragMode(QGraphicsView::ScrollHandDrag);
		views << view;
	}
	layout->addWidget(panes, 1);
	auto* zoom = new QSlider(Qt::Horizontal, dialog);
	zoom->setRange(10, 300); zoom->setValue(100);
	connect(zoom, &QSlider::valueChanged, dialog, [views](int percent)
	{
		for (auto* view : views) { view->resetTransform(); view->scale(percent / 100.0, percent / 100.0); }
	});
	layout->addWidget(zoom);
	dialog->show();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	if (m_git.busy())
	{
		if (!confirm(tr("Quit"), tr("A Git operation is running. Cancel it and close? Changes already made will remain."))) { event->ignore(); return; }
		m_git.cancel();
	}
	if (!qApp->property("noSettings").toBool()) QSettings().setValue("windowGeometry", saveGeometry());
	event->accept();
}
