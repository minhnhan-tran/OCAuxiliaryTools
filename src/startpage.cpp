#include "startpage.h"

#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QVBoxLayout>

#include "sysinfo.h"

StartPage::StartPage(QWidget* parent) : QWidget(parent) {
  auto* top = new QVBoxLayout(this);
  top->setContentsMargins(24, 18, 24, 18);

  auto* title = new QLabel(tr("OpenCore Auxiliary Tools"));
  QFont f = title->font();
  f.setPointSize(f.pointSize() + 8);
  f.setBold(true);
  title->setFont(f);
  top->addWidget(title);

  auto* sub = new QLabel(tr("No config.plist is open. This is the computer you are running on."));
  sub->setWordWrap(true);
  top->addWidget(sub);

  tree_ = new QTreeWidget;
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("Item"), tr("Value")});
  tree_->setRootIsDecorated(true);
  tree_->setAlternatingRowColors(true);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree_->setTextElideMode(Qt::ElideMiddle);
  tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  tree_->header()->setStretchLastSection(true);
  top->addWidget(tree_, 1);

  lblStatus_ = new QLabel;
  top->addWidget(lblStatus_);

  auto* row = new QHBoxLayout;
  auto* open = new QPushButton(tr("Open config.plist..."));
  auto* editor = new QPushButton(tr("Go to editor"));
  auto* copy = new QPushButton(tr("Copy"));
  btnRefresh_ = new QPushButton(tr("Refresh"));
  open->setDefault(true);
  row->addWidget(open);
  row->addWidget(editor);
  row->addStretch();
  row->addWidget(copy);
  row->addWidget(btnRefresh_);
  top->addLayout(row);

  connect(open, &QPushButton::clicked, this, &StartPage::openRequested);
  connect(editor, &QPushButton::clicked, this, &StartPage::editorRequested);
  connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(asText()); });
  connect(btnRefresh_, &QPushButton::clicked, this, &StartPage::refresh);
}

StartPage::~StartPage() {
  if (worker_ && worker_->isRunning()) {
    worker_->disconnect(this);
    if (!worker_->wait(2000)) {  // a probe is stuck; the app is quitting anyway
      worker_->terminate();
      worker_->wait(1000);
    }
  }
}

void StartPage::refresh() {
  if (worker_ && worker_->isRunning()) return;
  btnRefresh_->setEnabled(false);
  lblStatus_->setText(tr("Reading system information..."));
  delete worker_;
  worker_ = new SysInfoCollector(this);
  connect(worker_, &SysInfoCollector::collected, this, &StartPage::fill, Qt::QueuedConnection);
  worker_->start();
}

void StartPage::fill(const QVariantList& rows) {
  tree_->clear();
  QTreeWidgetItem* cur = nullptr;
  QString curSec;
  for (const QVariant& v : rows) {
    const QStringList r = v.toStringList();
    if (r.size() != 3) continue;
    if (!cur || r[0] != curSec) {
      cur = new QTreeWidgetItem(tree_, QStringList{r[0]});
      QFont bf = cur->font(0);
      bf.setBold(true);
      cur->setFont(0, bf);
      curSec = r[0];
    }
    auto* it = new QTreeWidgetItem(cur, QStringList{r[1], r[2]});
    it->setToolTip(1, r[2]);
  }
  tree_->expandAll();
  lblStatus_->clear();
  btnRefresh_->setEnabled(true);
}

QString StartPage::asText() const {
  QString t;
  for (int i = 0; i < tree_->topLevelItemCount(); i++) {
    const QTreeWidgetItem* s = tree_->topLevelItem(i);
    t += "[" + s->text(0) + "]\n";
    for (int j = 0; j < s->childCount(); j++) t += s->child(j)->text(0) + ": " + s->child(j)->text(1) + "\n";
    t += "\n";
  }
  return t.trimmed();
}
