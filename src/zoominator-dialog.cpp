#include "zoominator-dialog.hpp"

#include "zoominator-controller.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
static void frontend_event_cb(enum obs_frontend_event event, void *data)
{
	(void)event;
	auto *dlg = static_cast<ZoominatorDialog *>(data);
	if (!dlg)
		return;
	QMetaObject::invokeMethod(dlg, "refreshLists", Qt::QueuedConnection);
}

static bool is_capture_source_id(const char *id)
{
	if (!id)
		return false;
	return strcmp(id, "monitor_capture") == 0 || strcmp(id, "window_capture") == 0 ||
	       strcmp(id, "game_capture") == 0;
}
} // namespace

ZoominatorDialog::ZoominatorDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QStringLiteral("Zoominator"));
	setModal(false);
	resize(640, 520);

	buildUi();

	obs_frontend_add_event_callback(frontend_event_cb, this);
	refreshLists();
	loadFromController();
}

void ZoominatorDialog::closeEvent(QCloseEvent *event)
{
	obs_frontend_remove_event_callback(frontend_event_cb, this);
	applyToController();
	QDialog::closeEvent(event);
}

void ZoominatorDialog::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);

	auto *topBox = new QGroupBox("Target", this);
	auto *topLay = new QFormLayout(topBox);

	cmbSource = new QComboBox(topBox);
	cmbSource->setMinimumWidth(320);
	topLay->addRow("Capture Source", cmbSource);

	cmbMode = new QComboBox(topBox);
	cmbMode->addItem("Hold (press=zoom, release=restore)", "hold");
	cmbMode->addItem("Toggle (press=zoom, press again=restore)", "toggle");
	topLay->addRow("Behavior", cmbMode);

	root->addWidget(topBox);

	auto *trigBox = new QGroupBox("Trigger", this);
	auto *trigLay = new QVBoxLayout(trigBox);

	auto *rowType = new QHBoxLayout();
	cmbTrigger = new QComboBox(trigBox);
	cmbTrigger->addItem("Keyboard", "keyboard");
	cmbTrigger->addItem("Mouse Button", "mouse");
	rowType->addWidget(new QLabel("Type:", trigBox));
	rowType->addWidget(cmbTrigger, 1);
	trigLay->addLayout(rowType);

	auto *rowHot = new QHBoxLayout();
	editHotkey = new QKeySequenceEdit(trigBox);
	btnClearHotkey = new QPushButton("Clear", trigBox);
	btnClearHotkey->setToolTip("Clear keyboard hotkey");
	rowHot->addWidget(new QLabel("Hotkey:", trigBox));
	rowHot->addWidget(editHotkey, 1);
	rowHot->addWidget(btnClearHotkey, 0);
	trigLay->addLayout(rowHot);

	auto *rowMouse = new QHBoxLayout();
	cmbMouseBtn = new QComboBox(trigBox);
	cmbMouseBtn->addItem("Left", "left");
	cmbMouseBtn->addItem("Right", "right");
	cmbMouseBtn->addItem("Middle", "middle");
	cmbMouseBtn->addItem("X1", "x1");
	cmbMouseBtn->addItem("X2", "x2");
	rowMouse->addWidget(new QLabel("Mouse Button:", trigBox));
	rowMouse->addWidget(cmbMouseBtn, 1);
	trigLay->addLayout(rowMouse);

	auto *rowMods = new QHBoxLayout();
	chkCtrl = new QCheckBox("Ctrl", trigBox);
	chkAlt = new QCheckBox("Alt", trigBox);
	chkShift = new QCheckBox("Shift", trigBox);
	chkWin = new QCheckBox("Win", trigBox);
	rowMods->addWidget(new QLabel("Modifiers:", trigBox));
	rowMods->addWidget(chkCtrl);
	rowMods->addWidget(chkAlt);
	rowMods->addWidget(chkShift);
	rowMods->addWidget(chkWin);
	rowMods->addStretch(1);
	trigLay->addLayout(rowMods);

	root->addWidget(trigBox);

	auto *cfgBox = new QGroupBox("Zoom", this);
	auto *cfg = new QFormLayout(cfgBox);

	spZoom = new QDoubleSpinBox(cfgBox);
	spZoom->setRange(0.0, 8.0);
	spZoom->setSingleStep(0.05);
	spZoom->setDecimals(2);
	spZoom->setToolTip("Set to 0 or 1 to disable zoom and only follow the mouse. Values >1 zoom in.");
	cfg->addRow("Zoom Factor", spZoom);

	spIn = new QSpinBox(cfgBox);
	spIn->setRange(0, 5000);
	spIn->setSingleStep(10);
	cfg->addRow("Animation In (ms)", spIn);

	spOut = new QSpinBox(cfgBox);
	spOut->setRange(0, 5000);
	spOut->setSingleStep(10);
	cfg->addRow("Animation Out (ms)", spOut);

	chkFollow = new QCheckBox("Follow Mouse (when cursor is inside the captured region)", cfgBox);
	cfg->addRow(chkFollow);

	spFollowSpeed = new QDoubleSpinBox(cfgBox);
	spFollowSpeed->setRange(0.1, 40.0);
	spFollowSpeed->setSingleStep(0.5);
	spFollowSpeed->setDecimals(1);
	cfg->addRow("Follow Speed", spFollowSpeed);

	chkPortraitCover = new QCheckBox("Portrait canvas cover (auto scale to fill)", cfgBox);
	chkPortraitCover->setToolTip("When the base canvas is vertical (portrait), scale the capture so it fully covers the canvas (no top/bottom gaps).");
	cfg->addRow(chkPortraitCover);

	chkDebug = new QCheckBox("Debug Logging", cfgBox);
	cfg->addRow(chkDebug);

	root->addWidget(cfgBox);

	lblStatus = new QLabel(this);
	lblStatus->setWordWrap(true);
	lblStatus->setText(QStringLiteral("Tip: Use the Dock for fast switching between sources."));
	root->addWidget(lblStatus);

	auto *btnRow = new QHBoxLayout();
	btnRefresh = new QPushButton("Refresh Lists", this);
	btnApply = new QPushButton("Apply", this);
	btnTest = new QPushButton("Test", this);

	btnRow->addWidget(btnRefresh);
	btnRow->addStretch(1);
	btnRow->addWidget(btnTest);
	btnRow->addWidget(btnApply);

	root->addLayout(btnRow);

	connect(btnRefresh, &QPushButton::clicked, this, &ZoominatorDialog::refreshLists);
	connect(btnApply, &QPushButton::clicked, this, &ZoominatorDialog::applyToController);
	connect(btnTest, &QPushButton::clicked, this, &ZoominatorDialog::testZoom);
	connect(btnClearHotkey, &QPushButton::clicked, this, &ZoominatorDialog::clearHotkey);
}

void ZoominatorDialog::populateSources()
{
	const QString cur = cmbSource->currentData().toString();

	cmbSource->blockSignals(true);
	cmbSource->clear();
	cmbSource->addItem("(Select Source)", "");

	struct Ctx { QComboBox *cmb; };
	Ctx ctx{cmbSource};

	auto enum_cb = [](void *p, obs_source_t *src) -> bool {
		auto *c = static_cast<Ctx *>(p);
		if (!c || !src) return true;
		const char *id = obs_source_get_id(src);
		if (!is_capture_source_id(id))
			return true;
		const char *nm = obs_source_get_name(src);
		if (!nm || !*nm) return true;
		c->cmb->addItem(QString::fromUtf8(nm), QString::fromUtf8(nm));
		return true;
	};

	obs_enum_sources(enum_cb, &ctx);

	int idx = cmbSource->findData(cur);
	if (idx < 0) idx = cmbSource->findData(ZoominatorController::instance().sourceName);
	if (idx >= 0) cmbSource->setCurrentIndex(idx);

	cmbSource->blockSignals(false);
}

void ZoominatorDialog::refreshLists()
{
	populateSources();
}

void ZoominatorDialog::loadFromController()
{
	loading = true;
	auto &c = ZoominatorController::instance();

	refreshLists();

	int sidx = cmbSource->findData(c.sourceName);
	if (sidx >= 0) cmbSource->setCurrentIndex(sidx);

	int midx = cmbMode->findData(c.hotkeyMode);
	if (midx >= 0) cmbMode->setCurrentIndex(midx);

	int tidx = cmbTrigger->findData(c.triggerType);
	if (tidx >= 0) cmbTrigger->setCurrentIndex(tidx);

	int bidx = cmbMouseBtn->findData(c.mouseButton);
	if (bidx >= 0) cmbMouseBtn->setCurrentIndex(bidx);

	chkCtrl->setChecked(c.modCtrl);
	chkAlt->setChecked(c.modAlt);
	chkShift->setChecked(c.modShift);
	chkWin->setChecked(c.modWin);

	editHotkey->setKeySequence(QKeySequence(c.hotkeySequence));

	spZoom->setValue(c.zoomFactor);
	spIn->setValue(c.animInMs);
	spOut->setValue(c.animOutMs);
	chkFollow->setChecked(c.followMouse);
	spFollowSpeed->setValue(c.followSpeed);
	chkPortraitCover->setChecked(c.portraitCover);
	chkDebug->setChecked(c.debug);

	loading = false;
}

void ZoominatorDialog::applyToController()
{
	if (loading)
		return;

	auto &c = ZoominatorController::instance();
	c.sourceName = cmbSource->currentData().toString();
	c.hotkeyMode = cmbMode->currentData().toString();

	c.triggerType = cmbTrigger->currentData().toString();
	c.mouseButton = cmbMouseBtn->currentData().toString();
	c.modCtrl = chkCtrl->isChecked();
	c.modAlt = chkAlt->isChecked();
	c.modShift = chkShift->isChecked();
	c.modWin = chkWin->isChecked();

	c.hotkeySequence = editHotkey->keySequence().toString(QKeySequence::NativeText);

	c.zoomFactor = spZoom->value();
	c.animInMs = spIn->value();
	c.animOutMs = spOut->value();
	c.followMouse = chkFollow->isChecked();
	c.followSpeed = spFollowSpeed->value();
	c.portraitCover = chkPortraitCover->isChecked();
	c.debug = chkDebug->isChecked();

	c.saveSettings();
	lblStatus->setText(QStringLiteral("Applied. Use your trigger to zoom."));
}

void ZoominatorDialog::testZoom()
{
	applyToController();
	lblStatus->setText(QStringLiteral("Use your configured trigger to test zoom."));
}

void ZoominatorDialog::clearHotkey()
{
	editHotkey->setKeySequence(QKeySequence());
}
