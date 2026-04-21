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
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QString>
#include <QGuiApplication>
#include <QScreen>
#include <QColorDialog>
#include <QColor>

namespace {
static void frontend_event_cb(enum obs_frontend_event event, void *data)
{
	(void)event;
	auto *dlg = static_cast<ZoominatorDialog *>(data);
	if (!dlg)
		return;
	QMetaObject::invokeMethod(dlg, "refreshLists", Qt::QueuedConnection);
}

static QKeySequence sequence_without_modifiers(const QKeySequence &seq)
{
	if (seq.isEmpty())
		return {};
	const QKeyCombination kc = seq[0];
	if (kc.key() == Qt::Key_unknown)
		return {};
	return QKeySequence(QKeyCombination(Qt::NoModifier, kc.key()));
}

} // namespace

static bool key_sequence_is_modifier_only(const QKeySequence &seq)
{
	if (seq.isEmpty())
		return false;
	const QKeyCombination kc = seq[0];
	switch (kc.key()) {
	case Qt::Key_Control:
	case Qt::Key_Shift:
	case Qt::Key_Alt:
	case Qt::Key_Meta:
		return true;
	default:
		return false;
	}
}

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
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(8);

	auto *topBox = new QGroupBox("Target", this);
	auto *topLay = new QVBoxLayout(topBox);
	topLay->setContentsMargins(8, 8, 8, 8);
	topLay->setSpacing(6);

	auto *rowTarget = new QHBoxLayout();
	rowTarget->setSpacing(8);

	cmbSource = new QComboBox(topBox);
	cmbSource->setMinimumWidth(260);

	cmbMode = new QComboBox(topBox);
	cmbMode->addItem("Hold (press=zoom, release=restore)", "hold");
	cmbMode->addItem("Toggle (press=zoom, press again=restore)", "toggle");

	rowTarget->addWidget(new QLabel("Target Screen", topBox));
	rowTarget->addWidget(cmbSource, 1);
	rowTarget->addSpacing(6);
	rowTarget->addWidget(new QLabel("Behavior", topBox));
	rowTarget->addWidget(cmbMode, 0);

	topLay->addLayout(rowTarget);
	root->addWidget(topBox);

	auto *trigBox = new QGroupBox("Trigger", this);
	auto *trigLay = new QVBoxLayout(trigBox);
	trigLay->setContentsMargins(8, 8, 8, 8);
	trigLay->setSpacing(6);

	auto *rowType = new QHBoxLayout();
	rowType->setSpacing(8);
	cmbTrigger = new QComboBox(trigBox);
	cmbTrigger->addItem("Keyboard", "keyboard");
	cmbTrigger->addItem("Mouse Button", "mouse");
	rowType->addWidget(new QLabel("Type:", trigBox));
	rowType->addWidget(cmbTrigger, 1);
	trigLay->addLayout(rowType);

	rowHotkeyWidget = new QWidget(trigBox);
	auto *rowHot = new QHBoxLayout(rowHotkeyWidget);
	rowHot->setContentsMargins(0, 0, 0, 0);
	rowHot->setSpacing(8);
	editHotkey = new QKeySequenceEdit(rowHotkeyWidget);
	btnClearHotkey = new QPushButton("Clear", rowHotkeyWidget);
	btnClearHotkey->setToolTip("Clear keyboard hotkey");
	rowHot->addWidget(new QLabel("Hotkey:", rowHotkeyWidget));
	rowHot->addWidget(editHotkey, 1);
	rowHot->addWidget(btnClearHotkey, 0);
	trigLay->addWidget(rowHotkeyWidget);

	rowFollowToggleWidget = new QWidget(topBox);
	auto *rowFollowToggle = new QHBoxLayout(rowFollowToggleWidget);
	rowFollowToggle->setContentsMargins(0, 0, 0, 0);
	rowFollowToggle->setSpacing(8);
	editFollowToggleHotkey = new QKeySequenceEdit(rowFollowToggleWidget);
	btnClearFollowToggleHotkey = new QPushButton("Clear", rowFollowToggleWidget);
	btnClearFollowToggleHotkey->setToolTip("Clear follow mouse toggle hotkey");
	rowFollowToggle->addWidget(new QLabel("Follow Toggle Hotkey:", rowFollowToggleWidget));
	rowFollowToggle->addWidget(editFollowToggleHotkey, 1);
	rowFollowToggle->addWidget(btnClearFollowToggleHotkey, 0);
	topLay->addWidget(rowFollowToggleWidget);

	rowMouseWidget = new QWidget(trigBox);
	auto *rowMouse = new QHBoxLayout(rowMouseWidget);
	rowMouse->setContentsMargins(0, 0, 0, 0);
	rowMouse->setSpacing(8);
	cmbMouseBtn = new QComboBox(rowMouseWidget);
	cmbMouseBtn->addItem("Left", "left");
	cmbMouseBtn->addItem("Right", "right");
	cmbMouseBtn->addItem("Middle", "middle");
	cmbMouseBtn->addItem("X1", "x1");
	cmbMouseBtn->addItem("X2", "x2");
	rowMouse->addWidget(new QLabel("Mouse Button:", rowMouseWidget));
	rowMouse->addWidget(cmbMouseBtn, 1);
	trigLay->addWidget(rowMouseWidget);

	rowModifiersWidget = new QWidget(trigBox);
	auto *rowMods = new QGridLayout(rowModifiersWidget);
	rowMods->setContentsMargins(0, 0, 0, 0);
	rowMods->setHorizontalSpacing(8);
	rowMods->setVerticalSpacing(4);
	chkCtrl = new QCheckBox("Ctrl (Any)", rowModifiersWidget);
	chkLeftCtrl = new QCheckBox("L Ctrl", rowModifiersWidget);
	chkRightCtrl = new QCheckBox("R Ctrl", rowModifiersWidget);
	chkAlt = new QCheckBox("Alt (Any)", rowModifiersWidget);
	chkLeftAlt = new QCheckBox("L Alt", rowModifiersWidget);
	chkRightAlt = new QCheckBox("R Alt", rowModifiersWidget);
	chkShift = new QCheckBox("Shift (Any)", rowModifiersWidget);
	chkLeftShift = new QCheckBox("L Shift", rowModifiersWidget);
	chkRightShift = new QCheckBox("R Shift", rowModifiersWidget);
	chkWin = new QCheckBox("Meta/Win (Any)", rowModifiersWidget);
	chkLeftWin = new QCheckBox("L Meta/Win", rowModifiersWidget);
	chkRightWin = new QCheckBox("R Meta/Win", rowModifiersWidget);
	rowMods->addWidget(new QLabel("Modifiers:", rowModifiersWidget), 0, 0);
	rowMods->addWidget(chkCtrl, 0, 1);
	rowMods->addWidget(chkLeftCtrl, 0, 2);
	rowMods->addWidget(chkRightCtrl, 0, 3);
	rowMods->addWidget(chkAlt, 1, 1);
	rowMods->addWidget(chkLeftAlt, 1, 2);
	rowMods->addWidget(chkRightAlt, 1, 3);
	rowMods->addWidget(chkShift, 2, 1);
	rowMods->addWidget(chkLeftShift, 2, 2);
	rowMods->addWidget(chkRightShift, 2, 3);
	rowMods->addWidget(chkWin, 3, 1);
	rowMods->addWidget(chkLeftWin, 3, 2);
	rowMods->addWidget(chkRightWin, 3, 3);
	rowMods->addWidget(new QLabel("Use Any for legacy behavior, or choose left/right explicitly.",
				      rowModifiersWidget),
			   4, 1, 1, 3);
	rowMods->setColumnStretch(4, 1);
	trigLay->addWidget(rowModifiersWidget);

	root->addWidget(trigBox);

	auto *cfgBox = new QGroupBox("Zoom", this);
	auto *cfgLay = new QVBoxLayout(cfgBox);
	cfgLay->setContentsMargins(8, 8, 8, 8);
	cfgLay->setSpacing(6);

	auto *rowZoom = new QHBoxLayout();
	rowZoom->setSpacing(8);

	spZoom = new QDoubleSpinBox(cfgBox);
	spZoom->setRange(0.0, 8.0);
	spZoom->setSingleStep(0.05);
	spZoom->setDecimals(2);
	spZoom->setToolTip("Set to 0 or 1 to disable zoom and only follow the mouse. Values >1 zoom in.");
	spZoom->setMinimumWidth(90);

	spIn = new QSpinBox(cfgBox);
	spIn->setRange(0, 5000);
	spIn->setSingleStep(10);
	spIn->setMinimumWidth(80);

	spOut = new QSpinBox(cfgBox);
	spOut->setRange(0, 5000);
	spOut->setSingleStep(10);
	spOut->setMinimumWidth(80);

	rowZoom->addWidget(new QLabel("Zoom", cfgBox));
	rowZoom->addWidget(spZoom);
	rowZoom->addSpacing(8);
	rowZoom->addWidget(new QLabel("In (ms)", cfgBox));
	rowZoom->addWidget(spIn);
	rowZoom->addSpacing(8);
	rowZoom->addWidget(new QLabel("Out (ms)", cfgBox));
	rowZoom->addWidget(spOut);
	rowZoom->addStretch(1);

	cfgLay->addLayout(rowZoom);

	auto *rowFollow = new QHBoxLayout();
	rowFollow->setSpacing(8);

	chkFollow = new QCheckBox("Follow Mouse (when cursor is inside the captured region)", cfgBox);

	spFollowSpeed = new QDoubleSpinBox(cfgBox);
	spFollowSpeed->setRange(0.1, 40.0);
	spFollowSpeed->setSingleStep(0.5);
	spFollowSpeed->setDecimals(1);
	spFollowSpeed->setMinimumWidth(80);

	rowFollow->addWidget(chkFollow, 1);
	rowFollow->addWidget(new QLabel("Speed", cfgBox));
	rowFollow->addWidget(spFollowSpeed);

	cfgLay->addLayout(rowFollow);

	chkPortraitCover = new QCheckBox("Portrait canvas cover (auto scale to fill)", cfgBox);
	chkPortraitCover->setToolTip(
		"When the base canvas is vertical (portrait), scale the capture so it fully covers the canvas (no top/bottom gaps).");
	cfgLay->addWidget(chkPortraitCover);

	auto *rowMarkerFlags = new QHBoxLayout();
	rowMarkerFlags->setSpacing(8);
	chkShowCursorMarker = new QCheckBox("Show cursor halo", cfgBox);
	chkMarkerOnlyOnClick = new QCheckBox("Show only after click", cfgBox);
	chkMarkerOnlyOnClick->setToolTip(
		"When enabled, the halo flashes where you click instead of following the cursor continuously.");
	rowMarkerFlags->addWidget(chkShowCursorMarker);
	rowMarkerFlags->addWidget(chkMarkerOnlyOnClick);
	rowMarkerFlags->addStretch(1);
	cfgLay->addLayout(rowMarkerFlags);

	auto *rowHalo = new QHBoxLayout();
	rowHalo->setSpacing(8);
	spMarkerSize = new QSpinBox(cfgBox);
	spMarkerSize->setRange(6, 256);
	spMarkerSize->setSingleStep(2);
	spMarkerSize->setMinimumWidth(70);
	spMarkerThickness = new QSpinBox(cfgBox);
	spMarkerThickness->setRange(1, 64);
	spMarkerThickness->setSingleStep(1);
	spMarkerThickness->setMinimumWidth(70);
	btnMarkerColor = new QPushButton("Pick Color", cfgBox);
	btnMarkerColor->setMinimumHeight(26);
	btnMarkerColor->setMinimumWidth(120);
	rowHalo->addWidget(new QLabel("Halo Size", cfgBox));
	rowHalo->addWidget(spMarkerSize);
	rowHalo->addWidget(new QLabel("Thickness", cfgBox));
	rowHalo->addWidget(spMarkerThickness);
	rowHalo->addWidget(btnMarkerColor);
	rowHalo->addStretch(1);
	cfgLay->addLayout(rowHalo);

	chkDebug = new QCheckBox("Debug Logging", cfgBox);
	cfgLay->addWidget(chkDebug);

	root->addWidget(cfgBox);

	lblStatus = new QLabel(this);
	lblStatus->setWordWrap(true);
	lblStatus->setText(QStringLiteral("Tip: Select the monitor that should drive the scene movement."));
	root->addWidget(lblStatus);

	auto *btnRow = new QHBoxLayout();
	btnRow->setSpacing(8);

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
	connect(btnClearFollowToggleHotkey, &QPushButton::clicked, this, &ZoominatorDialog::clearFollowToggleHotkey);
	connect(btnMarkerColor, &QPushButton::clicked, this, &ZoominatorDialog::chooseMarkerColor);

	connect(cmbTrigger, &QComboBox::currentIndexChanged, this, [this](int) {
		const QString trigger = cmbTrigger->currentData().toString();
		const bool isMouse = (trigger == "mouse");

		if (rowMouseWidget)
			rowMouseWidget->setVisible(isMouse);
		if (rowHotkeyWidget)
			rowHotkeyWidget->setVisible(!isMouse);
		if (rowModifiersWidget)
			rowModifiersWidget->setVisible(isMouse);
		if (rowFollowToggleWidget)
			rowFollowToggleWidget->setVisible(true);
	});
}

void ZoominatorDialog::populateSources()
{
	const QString cur = cmbSource->currentData().toString();

	cmbSource->blockSignals(true);
	cmbSource->clear();
	cmbSource->addItem("(Select Screen)", "");

	const auto screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); ++i) {
		auto *screen = screens[i];
		if (!screen)
			continue;
		const QRect g = screen->geometry();
		const QString key = QStringLiteral("%1,%2,%3,%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
		QString label = QStringLiteral("%1: %2x%3 @ (%4, %5)")
					.arg(screen->name())
					.arg(g.width())
					.arg(g.height())
					.arg(g.x())
					.arg(g.y());
		cmbSource->addItem(label, key);
	}

	int idx = cmbSource->findData(cur);
	if (idx < 0)
		idx = cmbSource->findData(ZoominatorController::instance().screenKey);
	if (idx >= 0)
		cmbSource->setCurrentIndex(idx);

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

	int sidx = cmbSource->findData(c.screenKey);
	if (sidx >= 0)
		cmbSource->setCurrentIndex(sidx);

	int midx = cmbMode->findData(c.hotkeyMode);
	if (midx >= 0)
		cmbMode->setCurrentIndex(midx);

	int tidx = cmbTrigger->findData(c.triggerType);
	if (tidx >= 0)
		cmbTrigger->setCurrentIndex(tidx);

	int bidx = cmbMouseBtn->findData(c.mouseButton);
	if (bidx >= 0)
		cmbMouseBtn->setCurrentIndex(bidx);

	chkCtrl->setChecked(c.modCtrl);
	chkAlt->setChecked(c.modAlt);
	chkShift->setChecked(c.modShift);
	chkWin->setChecked(c.modWin);
	chkLeftCtrl->setChecked(c.modLeftCtrl);
	chkRightCtrl->setChecked(c.modRightCtrl);
	chkLeftAlt->setChecked(c.modLeftAlt);
	chkRightAlt->setChecked(c.modRightAlt);
	chkLeftShift->setChecked(c.modLeftShift);
	chkRightShift->setChecked(c.modRightShift);
	chkLeftWin->setChecked(c.modLeftWin);
	chkRightWin->setChecked(c.modRightWin);

	editHotkey->setKeySequence(QKeySequence(c.hotkeySequence));
	editFollowToggleHotkey->setKeySequence(QKeySequence(c.followToggleHotkeySequence));

	spZoom->setValue(c.zoomFactor);
	spIn->setValue(c.animInMs);
	spOut->setValue(c.animOutMs);
	chkFollow->setChecked(c.followMouse);
	spFollowSpeed->setValue(c.followSpeed);
	chkPortraitCover->setChecked(c.portraitCover);
	chkShowCursorMarker->setChecked(c.showCursorMarker);
	chkMarkerOnlyOnClick->setChecked(c.markerOnlyOnClick);
	spMarkerSize->setValue(c.markerSize);
	spMarkerThickness->setValue(c.markerThickness);
	updateMarkerColorButton(QColor::fromRgba(c.markerColor));
	chkDebug->setChecked(c.debug);

	const QString trigger = cmbTrigger->currentData().toString();
	const bool isMouse = (trigger == "mouse");

	if (rowMouseWidget)
		rowMouseWidget->setVisible(isMouse);
	if (rowHotkeyWidget)
		rowHotkeyWidget->setVisible(!isMouse);
	if (rowModifiersWidget)
		rowModifiersWidget->setVisible(isMouse);
	if (rowFollowToggleWidget)
		rowFollowToggleWidget->setVisible(true);

	loading = false;
}

void ZoominatorDialog::applyToController()
{
	if (loading)
		return;

	auto &c = ZoominatorController::instance();
	c.screenKey = cmbSource->currentData().toString();
	c.hotkeyMode = cmbMode->currentData().toString();

	c.triggerType = cmbTrigger->currentData().toString();
	c.mouseButton = cmbMouseBtn->currentData().toString();
	c.modCtrl = chkCtrl->isChecked();
	c.modAlt = chkAlt->isChecked();
	c.modShift = chkShift->isChecked();
	c.modWin = chkWin->isChecked();
	c.modLeftCtrl = chkLeftCtrl->isChecked();
	c.modRightCtrl = chkRightCtrl->isChecked();
	c.modLeftAlt = chkLeftAlt->isChecked();
	c.modRightAlt = chkRightAlt->isChecked();
	c.modLeftShift = chkLeftShift->isChecked();
	c.modRightShift = chkRightShift->isChecked();
	c.modLeftWin = chkLeftWin->isChecked();
	c.modRightWin = chkRightWin->isChecked();

	QKeySequence triggerSequence = editHotkey->keySequence();
	if (!key_sequence_is_modifier_only(triggerSequence) &&
	    (c.modCtrl || c.modAlt || c.modShift || c.modWin || c.modLeftCtrl || c.modRightCtrl || c.modLeftAlt ||
	     c.modRightAlt || c.modLeftShift || c.modRightShift || c.modLeftWin || c.modRightWin)) {
		triggerSequence = sequence_without_modifiers(triggerSequence);
	}
	c.hotkeySequence = triggerSequence.toString(QKeySequence::NativeText);
	c.followToggleHotkeySequence =
		editFollowToggleHotkey->keySequence().toString(QKeySequence::NativeText);

	c.zoomFactor = spZoom->value();
	c.animInMs = spIn->value();
	c.animOutMs = spOut->value();
	c.followMouse = chkFollow->isChecked();
	c.followSpeed = spFollowSpeed->value();
	c.portraitCover = chkPortraitCover->isChecked();
	c.showCursorMarker = chkShowCursorMarker->isChecked();
	c.markerOnlyOnClick = chkMarkerOnlyOnClick->isChecked();
	c.markerSize = spMarkerSize->value();
	c.markerThickness = spMarkerThickness->value();
	if (btnMarkerColor)
		c.markerColor = (uint32_t)btnMarkerColor->property("markerRgba").toUInt();
	c.debug = chkDebug->isChecked();

	c.saveSettings();
	lblStatus->setText(QStringLiteral(
		"Applied. Zoom now uses the selected screen, moves the whole scene, and can show the cursor halo again."));
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

void ZoominatorDialog::clearFollowToggleHotkey()
{
	editFollowToggleHotkey->setKeySequence(QKeySequence());
}

void ZoominatorDialog::updateMarkerColorButton(const QColor &color)
{
	if (!btnMarkerColor)
		return;
	const QColor c = color.isValid() ? color : QColor(255, 0, 0);
	btnMarkerColor->setProperty("markerRgba", c.rgba());
	btnMarkerColor->setText(QStringLiteral("%1 / %2 / %3").arg(c.red()).arg(c.green()).arg(c.blue()));
	btnMarkerColor->setStyleSheet(
		QStringLiteral(
			"QPushButton { background:%1; color:%2; border:1px solid palette(mid); padding:4px 8px; }")
			.arg(c.name(QColor::HexArgb))
			.arg((c.lightness() < 128) ? QStringLiteral("#ffffff") : QStringLiteral("#000000")));
}

void ZoominatorDialog::chooseMarkerColor()
{
	const uint rgba = btnMarkerColor ? btnMarkerColor->property("markerRgba").toUInt() : QColor(255, 0, 0).rgba();
	const QColor picked = QColorDialog::getColor(
		QColor::fromRgba(rgba), this, QStringLiteral("Pick Cursor Halo Color"), QColorDialog::ShowAlphaChannel);
	if (!picked.isValid())
		return;

	updateMarkerColorButton(picked);
}
