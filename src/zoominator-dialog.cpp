#include "zoominator-dialog.hpp"
#include "zoominator-controller.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs.h>

extern "C" {
struct calldata;
struct signal_handler;
void signal_handler_connect(struct signal_handler *handler, const char *signal,
			    void (*callback)(void *, struct calldata *), void *data);
void signal_handler_disconnect(struct signal_handler *handler, const char *signal,
			       void (*callback)(void *, struct calldata *), void *data);
}

#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMap>
#include <QMetaObject>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QString>

#include <algorithm>

namespace {

static QString T(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

static QWidget *mkField(const QString &labelText, QWidget *input)
{
	auto *container = new QWidget;
	auto *v = new QVBoxLayout(container);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(4);
	auto *lbl = new QLabel(labelText, container);
	v->addWidget(lbl);
	v->addWidget(input);
	return container;
}

static void addSection(QVBoxLayout *lay, const QString &title, bool firstSection = false)
{
	if (!firstSection) {
		auto *sep = new QFrame;
		sep->setFrameShape(QFrame::HLine);
		sep->setFrameShadow(QFrame::Sunken);
		lay->addSpacing(12);
		lay->addWidget(sep);
	}
	lay->addSpacing(14);
	auto *lbl = new QLabel(QStringLiteral("<b>%1</b>").arg(title));
	lay->addWidget(lbl);
	lay->addSpacing(10);
}

static void frontend_event_cb(enum obs_frontend_event, void *data)
{
	auto *dlg = static_cast<ZoominatorDialog *>(data);
	if (!dlg)
		return;
	QMetaObject::invokeMethod(dlg, "refreshLists", Qt::QueuedConnection);
}

static QString friendlySourceKind(const QString &kind)
{
	if (kind == "image_source")
		return T("SourceKind.Image");
	if (kind == "browser_source")
		return T("SourceKind.Browser");
	if (kind == "game_capture")
		return T("SourceKind.GameCapture");
	if (kind == "window_capture")
		return T("SourceKind.WindowCapture");
	if (kind == "monitor_capture" || kind == "display_capture" || kind == "screen_capture")
		return T("SourceKind.DisplayCapture");
	if (kind == "dshow_input" || kind == "av_capture_input" || kind == "video_capture_device")
		return T("SourceKind.VideoCapture");
	if (kind == "wasapi_input_capture" || kind == "coreaudio_input_capture")
		return T("SourceKind.AudioInput");
	if (kind == "wasapi_output_capture" || kind == "coreaudio_output_capture")
		return T("SourceKind.AudioOutput");
	if (kind == "scene")
		return T("SourceKind.Scene");
	if (kind == "group")
		return T("SourceKind.Group");
	if (kind == "text_gdiplus" || kind == "text_ft2_source")
		return T("SourceKind.Text");
	if (kind == "color_source")
		return T("SourceKind.Color");
	if (kind == "ffmpeg_source")
		return T("SourceKind.Media");
	if (kind == "vlc_source")
		return T("SourceKind.VlcMedia");
	if (kind == "slideshow")
		return T("SourceKind.ImageSlideshow");
	return kind;
}

} // namespace

ZoominatorDialog::ZoominatorDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(T("Zoominator"));
	setModal(false);
	resize(620, 560);

	buildUi();

	obs_frontend_add_event_callback(frontend_event_cb, this);

	auto *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_create", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_connect(sh, "source_destroy", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_connect(sh, "source_rename", &ZoominatorDialog::obsSourceChanged, this);

	refreshLists();
	loadFromController();
}

void ZoominatorDialog::closeEvent(QCloseEvent *event)
{
	obs_frontend_remove_event_callback(frontend_event_cb, this);

	auto *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_create", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_disconnect(sh, "source_destroy", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_disconnect(sh, "source_rename", &ZoominatorDialog::obsSourceChanged, this);

	applyToController();
	QDialog::closeEvent(event);
}

void ZoominatorDialog::obsSourceChanged(void *data, struct calldata *)
{
	auto *dlg = static_cast<ZoominatorDialog *>(data);
	if (dlg)
		QMetaObject::invokeMethod(dlg, "populateSourcesTab", Qt::QueuedConnection);
}

void ZoominatorDialog::buildUi()
{
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(8);

	tabWidget = new QTabWidget(this);

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(0);

		cmbSource = new QComboBox(page);
		lay->addWidget(mkField(T("Dialog.TargetScreen"), cmbSource));
		lay->addSpacing(16);

		cmbMode = new QComboBox(page);
		cmbMode->addItem(T("Dialog.Mode.Hold"), "hold");
		cmbMode->addItem(T("Dialog.Mode.Toggle"), "toggle");
		lay->addWidget(mkField(T("Dialog.Behavior"), cmbMode));
		lay->addSpacing(16);

		auto *followHkRow = new QWidget(page);
		{
			auto *h = new QHBoxLayout(followHkRow);
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(6);
			editFollowToggleHotkey = new QKeySequenceEdit(followHkRow);
			btnClearFollowToggleHotkey = new QPushButton(T("Dialog.Clear"), followHkRow);
			btnClearFollowToggleHotkey->setToolTip(T("Dialog.ClearFollowToggleTooltip"));
			h->addWidget(editFollowToggleHotkey, 1);
			h->addWidget(btnClearFollowToggleHotkey);
		}
		lay->addWidget(mkField(T("Dialog.FollowToggleHotkey"), followHkRow));

		lay->addStretch(1);
		tabWidget->addTab(page, T("Dialog.Tab.Target"));
	}

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(0);

		cmbTrigger = new QComboBox(page);
		cmbTrigger->addItem(T("Dialog.Trigger.Keyboard"), "keyboard");
		cmbTrigger->addItem(T("Dialog.Trigger.MouseButton"), "mouse");
		lay->addWidget(mkField(T("Dialog.TriggerType"), cmbTrigger));
		lay->addSpacing(16);

		rowHotkeyWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowHotkeyWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(4);
			v->addWidget(new QLabel(T("Dialog.Hotkey"), rowHotkeyWidget));
			auto *h = new QHBoxLayout;
			h->setSpacing(6);
			editHotkey = new QKeySequenceEdit(rowHotkeyWidget);
			btnClearHotkey = new QPushButton(T("Dialog.Clear"), rowHotkeyWidget);
			btnClearHotkey->setToolTip(T("Dialog.ClearHotkeyTooltip"));
			h->addWidget(editHotkey, 1);
			h->addWidget(btnClearHotkey);
			v->addLayout(h);
		}
		lay->addWidget(rowHotkeyWidget);

		rowMouseWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowMouseWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(4);
			v->addWidget(new QLabel(T("Dialog.MouseButton"), rowMouseWidget));
			cmbMouseBtn = new QComboBox(rowMouseWidget);
			cmbMouseBtn->addItem(T("Dialog.Mouse.Left"), "left");
			cmbMouseBtn->addItem(T("Dialog.Mouse.Right"), "right");
			cmbMouseBtn->addItem(T("Dialog.Mouse.Middle"), "middle");
			cmbMouseBtn->addItem("X1", "x1");
			cmbMouseBtn->addItem("X2", "x2");
			v->addWidget(cmbMouseBtn);
		}
		lay->addWidget(rowMouseWidget);
		lay->addSpacing(16);

		rowModifiersWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowModifiersWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(6);
			v->addWidget(new QLabel(T("Dialog.RequiredModifiers"), rowModifiersWidget));
			auto *grid = new QGridLayout;
			grid->setHorizontalSpacing(10);
			grid->setVerticalSpacing(4);
			chkCtrl = new QCheckBox(T("Dialog.Mod.CtrlAny"), rowModifiersWidget);
			chkLeftCtrl = new QCheckBox(T("Dialog.Mod.LeftCtrl"), rowModifiersWidget);
			chkRightCtrl = new QCheckBox(T("Dialog.Mod.RightCtrl"), rowModifiersWidget);
			chkAlt = new QCheckBox(T("Dialog.Mod.AltAny"), rowModifiersWidget);
			chkLeftAlt = new QCheckBox(T("Dialog.Mod.LeftAlt"), rowModifiersWidget);
			chkRightAlt = new QCheckBox(T("Dialog.Mod.RightAlt"), rowModifiersWidget);
			chkShift = new QCheckBox(T("Dialog.Mod.ShiftAny"), rowModifiersWidget);
			chkLeftShift = new QCheckBox(T("Dialog.Mod.LeftShift"), rowModifiersWidget);
			chkRightShift = new QCheckBox(T("Dialog.Mod.RightShift"), rowModifiersWidget);
			chkWin = new QCheckBox(T("Dialog.Mod.MetaWinAny"), rowModifiersWidget);
			chkLeftWin = new QCheckBox(T("Dialog.Mod.LeftMetaWin"), rowModifiersWidget);
			chkRightWin = new QCheckBox(T("Dialog.Mod.RightMetaWin"), rowModifiersWidget);
			grid->addWidget(chkCtrl, 0, 0);
			grid->addWidget(chkLeftCtrl, 0, 1);
			grid->addWidget(chkRightCtrl, 0, 2);
			grid->addWidget(chkAlt, 1, 0);
			grid->addWidget(chkLeftAlt, 1, 1);
			grid->addWidget(chkRightAlt, 1, 2);
			grid->addWidget(chkShift, 2, 0);
			grid->addWidget(chkLeftShift, 2, 1);
			grid->addWidget(chkRightShift, 2, 2);
			grid->addWidget(chkWin, 3, 0);
			grid->addWidget(chkLeftWin, 3, 1);
			grid->addWidget(chkRightWin, 3, 2);
			grid->addWidget(new QLabel(T("Dialog.ModifierHelp"), rowModifiersWidget), 4, 0, 1, 3);
			grid->setColumnStretch(3, 1);
			v->addLayout(grid);
		}
		lay->addWidget(rowModifiersWidget);

		lay->addStretch(1);
		tabWidget->addTab(page, T("Dialog.Tab.Trigger"));

		connect(cmbTrigger, &QComboBox::currentIndexChanged, this, [this](int) {
			const bool isMouse = (cmbTrigger->currentData().toString() == "mouse");
			rowHotkeyWidget->setVisible(!isMouse);
			rowMouseWidget->setVisible(isMouse);
			rowModifiersWidget->setVisible(isMouse);
		});
	}

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 8, 20, 20);
		lay->setSpacing(0);

		addSection(lay, T("Dialog.Section.Zoom"), true);

		spZoom = new QDoubleSpinBox(page);
		spZoom->setRange(0.0, 8.0);
		spZoom->setSingleStep(0.05);
		spZoom->setDecimals(2);
		spZoom->setToolTip(T("Dialog.ZoomFactorTooltip"));

		spWheelZoomInStep = new QDoubleSpinBox(page);
		spWheelZoomInStep->setRange(0.01, 2.0);
		spWheelZoomInStep->setSingleStep(0.05);
		spWheelZoomInStep->setDecimals(2);
		spWheelZoomInStep->setSuffix(QStringLiteral("x"));

		spWheelZoomOutStep = new QDoubleSpinBox(page);
		spWheelZoomOutStep->setRange(0.01, 2.0);
		spWheelZoomOutStep->setSingleStep(0.05);
		spWheelZoomOutStep->setDecimals(2);
		spWheelZoomOutStep->setSuffix(QStringLiteral("x"));

		spWheelZoomMinimum = new QDoubleSpinBox(page);
		spWheelZoomMinimum->setRange(1.0, 19.0);
		spWheelZoomMinimum->setSingleStep(0.1);
		spWheelZoomMinimum->setDecimals(1);
		spWheelZoomMinimum->setSuffix(QStringLiteral("x"));

		spWheelZoomMaximum = new QDoubleSpinBox(page);
		spWheelZoomMaximum->setRange(1.1, 20.0);
		spWheelZoomMaximum->setSingleStep(0.5);
		spWheelZoomMaximum->setDecimals(1);
		spWheelZoomMaximum->setSuffix(QStringLiteral("x"));

		spIn = new QSpinBox(page);
		spIn->setRange(0, 5000);
		spIn->setSingleStep(10);
		spIn->setSuffix(T("Unit.Milliseconds"));

		spOut = new QSpinBox(page);
		spOut->setRange(0, 5000);
		spOut->setSingleStep(10);
		spOut->setSuffix(T("Unit.Milliseconds"));

		auto *zoomRow = new QHBoxLayout;
		zoomRow->setSpacing(12);
		zoomRow->addWidget(mkField(T("Dialog.ZoomFactor"), spZoom), 1);
		zoomRow->addWidget(mkField(T("Dialog.AnimateIn"), spIn), 1);
		zoomRow->addWidget(mkField(T("Dialog.AnimateOut"), spOut), 1);
		lay->addLayout(zoomRow);
		lay->addSpacing(12);

		auto *wheelZoomRow = new QHBoxLayout;
		wheelZoomRow->setSpacing(12);
		wheelZoomRow->addWidget(mkField(T("Dialog.WheelZoomInStep"), spWheelZoomInStep), 1);
		wheelZoomRow->addWidget(mkField(T("Dialog.WheelZoomOutStep"), spWheelZoomOutStep), 1);
		wheelZoomRow->addWidget(mkField(T("Dialog.WheelZoomMinimum"), spWheelZoomMinimum), 1);
		wheelZoomRow->addWidget(mkField(T("Dialog.WheelZoomMaximum"), spWheelZoomMaximum), 1);
		lay->addLayout(wheelZoomRow);

		addSection(lay, T("Dialog.Section.MouseFollow"));

		cmbZoomAnchor = new QComboBox(page);
		cmbZoomAnchor->addItem(T("Dialog.ZoomAnchor.Center"), "center");
		cmbZoomAnchor->addItem(T("Dialog.ZoomAnchor.CursorStatic"), "cursor_static");
		cmbZoomAnchor->addItem(T("Dialog.ZoomAnchor.CursorFollow"), "cursor_follow");
		cmbZoomAnchor->setToolTip(T("Dialog.ZoomAnchorTooltip"));

		spFollowSpeed = new QDoubleSpinBox(page);
		spFollowSpeed->setRange(0.1, 40.0);
		spFollowSpeed->setSingleStep(0.5);
		spFollowSpeed->setDecimals(1);

		chkCenterCursorUntilEdge = new QCheckBox(T("Dialog.CenterCursorUntilEdge"), page);
		chkCenterCursorUntilEdge->setToolTip(T("Dialog.CenterCursorUntilEdgeTooltip"));

		spMouseIdleTimeout = new QSpinBox(page);
		spMouseIdleTimeout->setRange(0, 60000);
		spMouseIdleTimeout->setSingleStep(100);
		spMouseIdleTimeout->setSuffix(T("Unit.Milliseconds"));
		spMouseIdleTimeout->setSpecialValueText(T("Dialog.Disabled"));
		spMouseIdleTimeout->setToolTip(T("Dialog.MouseIdleTimeoutTooltip"));

		auto *followRow = new QHBoxLayout;
		followRow->setSpacing(12);

		followRow->addWidget(mkField(T("Dialog.ZoomAnchor"), cmbZoomAnchor), 1);
		followRow->addWidget(mkField(T("Dialog.SmoothingSpeed"), spFollowSpeed), 1);
		followRow->addWidget(mkField(T("Dialog.MouseIdleTimeout"), spMouseIdleTimeout), 1);
		lay->addLayout(followRow);
		lay->addWidget(chkCenterCursorUntilEdge);

		/* Smoothing and the idle freeze only mean something while the anchor
		 * actually tracks the cursor. */
		auto syncFollowOnlyFields = [this]() {
			const bool follows = (cmbZoomAnchor->currentData().toString() == "cursor_follow");
			spFollowSpeed->setEnabled(follows);
			spMouseIdleTimeout->setEnabled(follows);
		};
		connect(cmbZoomAnchor, &QComboBox::currentIndexChanged, this,
			[syncFollowOnlyFields](int) { syncFollowOnlyFields(); });
		syncFollowOnlyFields();

		addSection(lay, T("Dialog.Section.Canvas"));

		chkPortraitCover = new QCheckBox(T("Dialog.PortraitCover"), page);
		chkPortraitCover->setToolTip(T("Dialog.PortraitCoverTooltip"));
		lay->addWidget(chkPortraitCover);

		addSection(lay, T("Dialog.Section.CursorHalo"));

		chkShowCursorMarker = new QCheckBox(T("Dialog.ShowCursorHalo"), page);
		chkShowCursorMarker->setToolTip(T("Dialog.ShowCursorHaloTooltip"));

		auto *haloFlagsRow = new QHBoxLayout;
		haloFlagsRow->setSpacing(20);
		haloFlagsRow->addWidget(chkShowCursorMarker);
		haloFlagsRow->addStretch(1);
		lay->addLayout(haloFlagsRow);
		lay->addSpacing(10);

		spMarkerSize = new QSpinBox(page);
		spMarkerSize->setRange(6, 256);
		spMarkerSize->setSingleStep(2);
		spMarkerSize->setSuffix(T("Unit.Pixels"));

		spMarkerThickness = new QSpinBox(page);
		spMarkerThickness->setRange(1, 64);
		spMarkerThickness->setSingleStep(1);
		spMarkerThickness->setSuffix(T("Unit.Pixels"));

		btnMarkerColor = new QPushButton(page);
		btnMarkerColor->setMinimumHeight(28);

		auto *haloRow = new QHBoxLayout;
		haloRow->setSpacing(12);
		haloRow->addWidget(mkField(T("Dialog.HaloSize"), spMarkerSize), 1);
		haloRow->addWidget(mkField(T("Dialog.RingThickness"), spMarkerThickness), 1);
		haloRow->addWidget(mkField(T("Dialog.Color"), btnMarkerColor), 1);
		lay->addLayout(haloRow);

		addSection(lay, T("Dialog.Section.Developer"));

		chkDebug = new QCheckBox(T("Dialog.EnableDebugLogging"), page);
		lay->addWidget(chkDebug);

		lay->addStretch(1);
		tabWidget->addTab(page, T("Dialog.Tab.Advanced"));
	}

	{
		auto *page = new QWidget;
		auto *lay = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(10);

		auto *info = new QLabel(T("Dialog.SourcesHelp"), page);
		info->setWordWrap(true);
		lay->addWidget(info);

		lstSources = new QListWidget(page);
		lstSources->setAlternatingRowColors(true);
		lstSources->setSortingEnabled(false);
		lay->addWidget(lstSources, 1);

		tabWidget->addTab(page, T("Dialog.Tab.Sources"));
	}

	root->addWidget(tabWidget, 1);

	lblStatus = new QLabel(this);
	lblStatus->setWordWrap(true);
	lblStatus->setText(T("Dialog.StatusTip"));
	root->addWidget(lblStatus);

	auto *btnRow = new QHBoxLayout;
	btnRow->setSpacing(8);
	btnRefresh = new QPushButton(T("Dialog.RefreshLists"), this);
	btnApply = new QPushButton(T("Dialog.Apply"), this);
	btnTest = new QPushButton(T("Dialog.Test"), this);
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
}

void ZoominatorDialog::populateSourcesTab()
{
	if (!lstSources)
		return;

	auto &c = ZoominatorController::instance();

	QSet<QString> included;
	if (lstSources->count() > 0) {
		for (int i = 0; i < lstSources->count(); i++) {
			const QListWidgetItem *it = lstSources->item(i);
			if (it && it->checkState() == Qt::Checked)
				included.insert(it->data(Qt::UserRole).toString());
		}
	} else {
		included = c.includedSources;
	}

	lstSources->clear();

	struct Collector {
		QMap<QString, QString> *map = nullptr;

		static void visitScene(obs_scene_t *scene, Collector *col)
		{
			if (!scene || !col || !col->map)
				return;
			obs_scene_enum_items(
				scene,
				[](obs_scene_t *, obs_sceneitem_t *item, void *p) -> bool {
					auto *col2 = static_cast<Collector *>(p);
					if (!item || !col2 || !col2->map)
						return true;

					obs_source_t *src = obs_sceneitem_get_source(item);
					if (!src)
						return true;

					const char *name = obs_source_get_name(src);
					if (!name || !*name)
						return true;

					if (strcmp(name, "Zoominator Cursor Marker") == 0)
						return true;

					const QString qname = QString::fromUtf8(name);
					if (!col2->map->contains(qname)) {
						const char *id = obs_source_get_id(src);
						col2->map->insert(qname, id ? QString::fromUtf8(id) : QString());
					}

					if (obs_scene_t *sub = obs_scene_from_source(src))
						Collector::visitScene(sub, col2);

					return true;
				},
				col);
		}
	};

	QMap<QString, QString> sourceMap;
	Collector collector{&sourceMap};

	obs_enum_scenes(
		[](void *p, obs_source_t *sceneSrc) -> bool {
			auto *col = static_cast<Collector *>(p);
			if (sceneSrc && col)
				if (obs_scene_t *scene = obs_scene_from_source(sceneSrc))
					Collector::visitScene(scene, col);
			return true;
		},
		&collector);

	QStringList names = sourceMap.keys();
	names.sort(Qt::CaseInsensitive);

	for (const QString &name : names) {
		const QString kind = friendlySourceKind(sourceMap.value(name));
		const QString label = kind.isEmpty() ? name : QStringLiteral("%1  [%2]").arg(name, kind);

		auto *litem = new QListWidgetItem(label, lstSources);
		litem->setData(Qt::UserRole, name);
		litem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		litem->setCheckState(included.contains(name) ? Qt::Checked : Qt::Unchecked);
	}
}

void ZoominatorDialog::populateSources()
{
	const QString cur = cmbSource->currentData().toString();

	cmbSource->blockSignals(true);
	cmbSource->clear();
	cmbSource->addItem(T("Dialog.SelectScreen"), "");

	const auto screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); ++i) {
		auto *screen = screens[i];
		if (!screen)
			continue;
		const QRect g = screen->geometry();
		const QString key = QStringLiteral("%1,%2,%3,%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
		const QString label = QStringLiteral("%1: %2×%3 @ (%4, %5)")
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
	populateSourcesTab();
}

void ZoominatorDialog::loadFromController()
{
	loading = true;
	auto &c = ZoominatorController::instance();

	refreshLists();

	{
		int idx = cmbSource->findData(c.screenKey);
		if (idx >= 0)
			cmbSource->setCurrentIndex(idx);

		idx = cmbMode->findData(c.hotkeyMode);
		if (idx >= 0)
			cmbMode->setCurrentIndex(idx);

		editFollowToggleHotkey->setKeySequence(QKeySequence(c.followToggleHotkeySequence));
	}

	{
		int idx = cmbTrigger->findData(c.triggerType);
		if (idx >= 0)
			cmbTrigger->setCurrentIndex(idx);

		idx = cmbMouseBtn->findData(c.mouseButton);
		if (idx >= 0)
			cmbMouseBtn->setCurrentIndex(idx);

		chkCtrl->setChecked(c.modCtrl);
		chkLeftCtrl->setChecked(c.modLeftCtrl);
		chkAlt->setChecked(c.modAlt);
		chkLeftAlt->setChecked(c.modLeftAlt);
		chkShift->setChecked(c.modShift);
		chkLeftShift->setChecked(c.modLeftShift);
		chkWin->setChecked(c.modWin);
		chkLeftWin->setChecked(c.modLeftWin);
		chkRightCtrl->setChecked(c.modRightCtrl);
		chkRightAlt->setChecked(c.modRightAlt);
		chkRightShift->setChecked(c.modRightShift);
		chkRightWin->setChecked(c.modRightWin);

		editHotkey->setKeySequence(QKeySequence(c.hotkeySequence));

		const bool isMouse = (c.triggerType == "mouse");
		rowHotkeyWidget->setVisible(!isMouse);
		rowMouseWidget->setVisible(isMouse);
		rowModifiersWidget->setVisible(isMouse);
	}

	{
		spZoom->setValue(c.zoomFactor);
		spWheelZoomInStep->setValue(c.wheelZoomInStep);
		spWheelZoomOutStep->setValue(c.wheelZoomOutStep);
		spWheelZoomMinimum->setValue(c.wheelZoomMinimum);
		spWheelZoomMaximum->setValue(c.wheelZoomMaximum);
		spIn->setValue(c.animInMs);
		spOut->setValue(c.animOutMs);
		const int anchorIdx =
			cmbZoomAnchor->findData(ZoominatorController::zoomAnchorModeToString(c.zoomAnchor));
		cmbZoomAnchor->setCurrentIndex(anchorIdx >= 0 ? anchorIdx : 2);
		spFollowSpeed->setValue(c.followSpeed);
		chkCenterCursorUntilEdge->setChecked(c.centerCursorUntilEdge);
		spMouseIdleTimeout->setValue(c.mouseIdleTimeoutMs);
		chkPortraitCover->setChecked(c.portraitCover);
		chkShowCursorMarker->setChecked(c.showCursorMarker);
		spMarkerSize->setValue(c.markerSize);
		spMarkerThickness->setValue(c.markerThickness);
		updateMarkerColorButton(QColor::fromRgba(c.markerColor));
		chkDebug->setChecked(c.debug);
	}

	if (lstSources)
		lstSources->clear();
	populateSourcesTab();

	loading = false;
}

void ZoominatorDialog::applyToController()
{
	if (loading)
		return;

	auto &c = ZoominatorController::instance();

	c.screenKey = cmbSource->currentData().toString();
	c.hotkeyMode = cmbMode->currentData().toString();
	c.followToggleHotkeySequence = editFollowToggleHotkey->keySequence().toString(QKeySequence::NativeText);

	c.triggerType = cmbTrigger->currentData().toString();
	c.mouseButton = cmbMouseBtn->currentData().toString();

	c.modCtrl = chkCtrl->isChecked();
	c.modLeftCtrl = chkLeftCtrl->isChecked();
	c.modAlt = chkAlt->isChecked();
	c.modLeftAlt = chkLeftAlt->isChecked();
	c.modShift = chkShift->isChecked();
	c.modLeftShift = chkLeftShift->isChecked();
	c.modWin = chkWin->isChecked();
	c.modLeftWin = chkLeftWin->isChecked();
	c.modRightCtrl = chkRightCtrl->isChecked();
	c.modRightAlt = chkRightAlt->isChecked();
	c.modRightShift = chkRightShift->isChecked();
	c.modRightWin = chkRightWin->isChecked();

	c.hotkeySequence = editHotkey->keySequence().toString(QKeySequence::NativeText);

	c.zoomFactor = spZoom->value();
	c.wheelZoomInStep = spWheelZoomInStep->value();
	c.wheelZoomOutStep = spWheelZoomOutStep->value();
	c.wheelZoomMinimum = spWheelZoomMinimum->value();
	c.wheelZoomMaximum = std::max(spWheelZoomMaximum->value(), c.wheelZoomMinimum + 0.1);
	c.animInMs = spIn->value();
	c.animOutMs = spOut->value();
	c.zoomAnchor = ZoominatorController::zoomAnchorModeFromString(cmbZoomAnchor->currentData().toString());
	c.followSpeed = spFollowSpeed->value();
	c.centerCursorUntilEdge = chkCenterCursorUntilEdge->isChecked();
	c.mouseIdleTimeoutMs = spMouseIdleTimeout->value();
	c.portraitCover = chkPortraitCover->isChecked();
	c.showCursorMarker = chkShowCursorMarker->isChecked();
	c.markerOnlyOnClick = true;
	c.markerSize = spMarkerSize->value();
	c.markerThickness = spMarkerThickness->value();
	if (btnMarkerColor)
		c.markerColor = (uint32_t)btnMarkerColor->property("markerRgba").toUInt();
	c.debug = chkDebug->isChecked();

	c.includedSources.clear();
	if (lstSources) {
		for (int i = 0; i < lstSources->count(); i++) {
			const QListWidgetItem *it = lstSources->item(i);
			if (it && it->checkState() == Qt::Checked)
				c.includedSources.insert(it->data(Qt::UserRole).toString());
		}
	}

	c.saveSettings();
	c.rebuildRuntimeHooks();
	lblStatus->setText(T("Dialog.SettingsApplied"));
}

void ZoominatorDialog::testZoom()
{
	applyToController();
	lblStatus->setText(T("Dialog.TestStatus"));
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
	btnMarkerColor->setStyleSheet(QStringLiteral("QPushButton { background:%1; color:%2;"
						     " border:1px solid palette(mid); padding:4px 8px; }")
					      .arg(c.name(QColor::HexArgb))
					      .arg(c.lightness() < 128 ? "#ffffff" : "#000000"));
}

void ZoominatorDialog::chooseMarkerColor()
{
	const uint rgba = btnMarkerColor ? btnMarkerColor->property("markerRgba").toUInt() : QColor(255, 0, 0).rgba();
	const QColor picked = QColorDialog::getColor(QColor::fromRgba(rgba), this, T("Dialog.PickCursorHaloColor"),
						     QColorDialog::ShowAlphaChannel);
	if (picked.isValid())
		updateMarkerColorButton(picked);
}
