#include "zoominator-dock.hpp"
#include "zoominator-controller.hpp"

#include <obs.h>
#include <obs-frontend-api.h>

#include <cstring>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QAbstractItemView>

namespace {

static bool is_capture_source_id(const char *id)
{
	if (!id)
		return false;
	return strcmp(id, "monitor_capture") == 0 || strcmp(id, "window_capture") == 0 ||
	       strcmp(id, "game_capture") == 0;
}

} // namespace

ZoominatorDock::ZoominatorDock(QWidget *parent) : QWidget(parent)
{
	buildUi();
	obs_frontend_add_event_callback(&ZoominatorDock::onFrontendEvent, this);

	refreshLists();
	loadFromController();
}

ZoominatorDock::~ZoominatorDock()
{
	obs_frontend_remove_event_callback(&ZoominatorDock::onFrontendEvent, this);
}

void ZoominatorDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	refreshLists();
	loadFromController();
}

void ZoominatorDock::onFrontendEvent(enum obs_frontend_event event, void *private_data)
{
	auto *dock = reinterpret_cast<ZoominatorDock *>(private_data);
	if (!dock)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		QMetaObject::invokeMethod(
			dock,
			[dock]() {
				dock->refreshLists();
				dock->loadFromController();
			},
			Qt::QueuedConnection);
		break;
	default:
		break;
	}
}

void ZoominatorDock::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	auto *panel = new QFrame(this);
	panel->setObjectName("zoominatorPanel");
	panel->setFrameShape(QFrame::NoFrame);

	auto *panelLay = new QVBoxLayout(panel);
	panelLay->setContentsMargins(12, 12, 12, 12);
	panelLay->setSpacing(8);

	cmbSource = new QComboBox(panel);
	cmbSource->setObjectName("zoominatorCombo");
	cmbSource->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	cmbSource->setMinimumWidth(240);

	btnRefresh = new QPushButton(tr("Refresh list"), panel);
	btnRefresh->setObjectName("zoominatorBtn");
	btnRefresh->setMinimumHeight(28);

	panelLay->addWidget(cmbSource);
	panelLay->addWidget(btnRefresh);
	panelLay->addStretch(1);

	root->addWidget(panel);

	panel->setStyleSheet(R"(
		#zoominatorPanel {
			border-bottom-left-radius: 3px;
			border-bottom-right-radius: 3px;
			background-color: #272a33;
      		border: 1px solid #3c404d;
		}

		#zoominatorCombo {
			padding: 4px 8px;
		}

		#zoominatorBtn {
			padding: 6px 10px;
			text-align: center;
		}
	)");

	connect(btnRefresh, &QPushButton::clicked, this, &ZoominatorDock::refreshLists);
	connect(cmbSource, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ZoominatorDock::onSourceChanged);
}

void ZoominatorDock::populateSources()
{
	cmbSource->clear();

	cmbSource->addItem(tr("Select Media Source..."));

	obs_enum_sources(
		[](void *param, obs_source_t *s) -> bool {
			auto *dock = reinterpret_cast<ZoominatorDock *>(param);
			const char *id = obs_source_get_id(s);
			if (!is_capture_source_id(id))
				return true;

			const char *name = obs_source_get_name(s);
			if (name && *name)
				dock->cmbSource->addItem(QString::fromUtf8(name));

			return true;
		},
		this);

	if (cmbSource->count() == 1) {
		cmbSource->addItem(tr("(no capture sources found)"));
		cmbSource->setEnabled(false);
	} else {
		cmbSource->setEnabled(true);
	}
}

void ZoominatorDock::refreshLists()
{
	loading = true;
	populateSources();
	loading = false;
}

void ZoominatorDock::loadFromController()
{
	auto &ctl = ZoominatorController::instance();
	const QString cur = ctl.sourceName;

	loading = true;
	int idx = cur.isEmpty() ? 0 : cmbSource->findText(cur);

	if (idx >= 0) {
		cmbSource->setCurrentIndex(idx);
	} else {
		cmbSource->setCurrentIndex(0);
	}
	loading = false;
}

void ZoominatorDock::onSourceChanged(int index)
{
	if (loading || !cmbSource->isEnabled())
		return;

	if (index == 0)
		return;

	const QString name = cmbSource->currentText();
	if (name.isEmpty() || name.startsWith("("))
		return;

	auto &ctl = ZoominatorController::instance();
	ctl.sourceName = name;
	ctl.saveSettings();
	ctl.notifySettingsChanged();
}