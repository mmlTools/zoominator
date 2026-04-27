#include "zoominator-dialog.hpp"
#include "zoominator-controller.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

extern "C" {
struct calldata;
struct signal_handler;
void signal_handler_connect(struct signal_handler *handler, const char *signal, void (*callback)(void *, struct calldata *), void *data);
void signal_handler_disconnect(struct signal_handler *handler, const char *signal, void (*callback)(void *, struct calldata *), void *data);
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

namespace {

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

static QKeySequence sequence_without_modifiers(const QKeySequence &seq)
{
	if (seq.isEmpty())
		return {};
	const QKeyCombination kc = seq[0];
	if (kc.key() == Qt::Key_unknown)
		return {};
	return QKeySequence(QKeyCombination(Qt::NoModifier, kc.key()));
}

static bool key_sequence_is_modifier_only(const QKeySequence &seq)
{
	if (seq.isEmpty())
		return false;
	switch (seq[0].key()) {
	case Qt::Key_Control:
	case Qt::Key_Shift:
	case Qt::Key_Alt:
	case Qt::Key_Meta:
		return true;
	default:
		return false;
	}
}

static QString friendlySourceKind(const QString &kind)
{
	if (kind == "image_source")                                   return "Image";
	if (kind == "browser_source")                                 return "Browser";
	if (kind == "game_capture")                                   return "Game Capture";
	if (kind == "window_capture")                                 return "Window Capture";
	if (kind == "monitor_capture" || kind == "display_capture" ||
	    kind == "screen_capture")                                 return "Display Capture";
	if (kind == "dshow_input" || kind == "av_capture_input" ||
	    kind == "video_capture_device")                           return "Video Capture";
	if (kind == "wasapi_input_capture" ||
	    kind == "coreaudio_input_capture")                        return "Audio Input";
	if (kind == "wasapi_output_capture" ||
	    kind == "coreaudio_output_capture")                       return "Audio Output";
	if (kind == "scene")                                          return "Scene";
	if (kind == "group")                                          return "Group";
	if (kind == "text_gdiplus" || kind == "text_ft2_source")     return "Text";
	if (kind == "color_source")                                   return "Color";
	if (kind == "ffmpeg_source")                                  return "Media";
	if (kind == "vlc_source")                                     return "VLC Media";
	if (kind == "slideshow")                                      return "Image Slideshow";
	return kind; 
}

} 

ZoominatorDialog::ZoominatorDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(QStringLiteral("Zoominator"));
	setModal(false);
	resize(620, 560);

	buildUi();

	
	obs_frontend_add_event_callback(frontend_event_cb, this);

	
	
	auto *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_create",  &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_connect(sh, "source_destroy", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_connect(sh, "source_rename",  &ZoominatorDialog::obsSourceChanged, this);

	refreshLists();
	loadFromController();
}

void ZoominatorDialog::closeEvent(QCloseEvent *event)
{
	obs_frontend_remove_event_callback(frontend_event_cb, this);

	auto *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_create",  &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_disconnect(sh, "source_destroy", &ZoominatorDialog::obsSourceChanged, this);
	signal_handler_disconnect(sh, "source_rename",  &ZoominatorDialog::obsSourceChanged, this);

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
		auto *lay  = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(0);

		
		cmbSource = new QComboBox(page);
		lay->addWidget(mkField("Target Screen", cmbSource));
		lay->addSpacing(16);

		
		cmbMode = new QComboBox(page);
		cmbMode->addItem("Hold: press to zoom, release to restore", "hold");
		cmbMode->addItem("Toggle: first press zooms, second restores", "toggle");
		lay->addWidget(mkField("Behavior", cmbMode));
		lay->addSpacing(16);

		
		auto *followHkRow = new QWidget(page);
		{
			auto *h = new QHBoxLayout(followHkRow);
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(6);
			editFollowToggleHotkey     = new QKeySequenceEdit(followHkRow);
			btnClearFollowToggleHotkey = new QPushButton("Clear", followHkRow);
			btnClearFollowToggleHotkey->setToolTip("Clear follow-mouse toggle hotkey");
			h->addWidget(editFollowToggleHotkey, 1);
			h->addWidget(btnClearFollowToggleHotkey);
		}
		lay->addWidget(mkField("Follow Mouse Toggle Hotkey", followHkRow));

		lay->addStretch(1);
		tabWidget->addTab(page, "Target");
	}

	
	
	
	{
		auto *page = new QWidget;
		auto *lay  = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(0);

		
		cmbTrigger = new QComboBox(page);
		cmbTrigger->addItem("Keyboard", "keyboard");
		cmbTrigger->addItem("Mouse Button", "mouse");
		lay->addWidget(mkField("Trigger Type", cmbTrigger));
		lay->addSpacing(16);

		
		rowHotkeyWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowHotkeyWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(4);
			v->addWidget(new QLabel("Hotkey", rowHotkeyWidget));
			auto *h = new QHBoxLayout;
			h->setSpacing(6);
			editHotkey    = new QKeySequenceEdit(rowHotkeyWidget);
			btnClearHotkey = new QPushButton("Clear", rowHotkeyWidget);
			btnClearHotkey->setToolTip("Clear keyboard hotkey");
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
			v->addWidget(new QLabel("Mouse Button", rowMouseWidget));
			cmbMouseBtn = new QComboBox(rowMouseWidget);
			cmbMouseBtn->addItem("Left",   "left");
			cmbMouseBtn->addItem("Right",  "right");
			cmbMouseBtn->addItem("Middle", "middle");
			cmbMouseBtn->addItem("X1",     "x1");
			cmbMouseBtn->addItem("X2",     "x2");
			v->addWidget(cmbMouseBtn);
		}
		lay->addWidget(rowMouseWidget);
		lay->addSpacing(16);

		
		rowModifiersWidget = new QWidget(page);
		{
			auto *v = new QVBoxLayout(rowModifiersWidget);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(6);
			v->addWidget(new QLabel("Required Modifiers", rowModifiersWidget));
			auto *grid = new QGridLayout;
			grid->setHorizontalSpacing(10);
			grid->setVerticalSpacing(4);
			chkCtrl       = new QCheckBox("Ctrl (Any)",     rowModifiersWidget);
			chkLeftCtrl   = new QCheckBox("L Ctrl",         rowModifiersWidget);
			chkRightCtrl  = new QCheckBox("R Ctrl",         rowModifiersWidget);
			chkAlt        = new QCheckBox("Alt (Any)",      rowModifiersWidget);
			chkLeftAlt    = new QCheckBox("L Alt",          rowModifiersWidget);
			chkRightAlt   = new QCheckBox("R Alt",          rowModifiersWidget);
			chkShift      = new QCheckBox("Shift (Any)",    rowModifiersWidget);
			chkLeftShift  = new QCheckBox("L Shift",        rowModifiersWidget);
			chkRightShift = new QCheckBox("R Shift",        rowModifiersWidget);
			chkWin        = new QCheckBox("Meta/Win (Any)", rowModifiersWidget);
			chkLeftWin    = new QCheckBox("L Meta/Win",     rowModifiersWidget);
			chkRightWin   = new QCheckBox("R Meta/Win",     rowModifiersWidget);
			grid->addWidget(chkCtrl,       0, 0); grid->addWidget(chkLeftCtrl,   0, 1); grid->addWidget(chkRightCtrl,  0, 2);
			grid->addWidget(chkAlt,        1, 0); grid->addWidget(chkLeftAlt,    1, 1); grid->addWidget(chkRightAlt,   1, 2);
			grid->addWidget(chkShift,      2, 0); grid->addWidget(chkLeftShift,  2, 1); grid->addWidget(chkRightShift, 2, 2);
			grid->addWidget(chkWin,        3, 0); grid->addWidget(chkLeftWin,    3, 1); grid->addWidget(chkRightWin,   3, 2);
			grid->addWidget(
				new QLabel("Use Any for legacy behaviour, or choose L / R explicitly.",
				           rowModifiersWidget),
				4, 0, 1, 3);
			grid->setColumnStretch(3, 1);
			v->addLayout(grid);
		}
		lay->addWidget(rowModifiersWidget);

		lay->addStretch(1);
		tabWidget->addTab(page, "Trigger");

		
		connect(cmbTrigger, &QComboBox::currentIndexChanged, this, [this](int) {
			const bool isMouse = (cmbTrigger->currentData().toString() == "mouse");
			rowHotkeyWidget->setVisible(!isMouse);
			rowMouseWidget->setVisible(isMouse);
			rowModifiersWidget->setVisible(isMouse);
		});
	}

	
	
	
	{
		auto *page = new QWidget;
		auto *lay  = new QVBoxLayout(page);
		lay->setContentsMargins(20, 8, 20, 20);
		lay->setSpacing(0);

		
		addSection(lay, "Zoom", true);

		
		spZoom = new QDoubleSpinBox(page);
		spZoom->setRange(0.0, 8.0);
		spZoom->setSingleStep(0.05);
		spZoom->setDecimals(2);
		spZoom->setToolTip("Values > 1 zoom in. Set to 0 or 1 to follow-only.");

		spIn = new QSpinBox(page);
		spIn->setRange(0, 5000);
		spIn->setSingleStep(10);
		spIn->setSuffix(" ms");

		spOut = new QSpinBox(page);
		spOut->setRange(0, 5000);
		spOut->setSingleStep(10);
		spOut->setSuffix(" ms");

		auto *zoomRow = new QHBoxLayout;
		zoomRow->setSpacing(12);
		zoomRow->addWidget(mkField("Zoom Factor",  spZoom),  1);
		zoomRow->addWidget(mkField("Animate In",   spIn),    1);
		zoomRow->addWidget(mkField("Animate Out",  spOut),   1);
		lay->addLayout(zoomRow);

		
		addSection(lay, "Mouse Follow");

		chkFollow = new QCheckBox("Enable", page);
		chkFollow->setToolTip("Follow the cursor while it stays inside the captured region.");

		spFollowSpeed = new QDoubleSpinBox(page);
		spFollowSpeed->setRange(0.1, 40.0);
		spFollowSpeed->setSingleStep(0.5);
		spFollowSpeed->setDecimals(1);

		auto *followRow = new QHBoxLayout;
		followRow->setSpacing(12);
		
		auto *followEnableW = new QWidget(page);
		{
			auto *v = new QVBoxLayout(followEnableW);
			v->setContentsMargins(0, 0, 0, 0);
			v->setSpacing(4);
			v->addWidget(new QLabel("Follow Mouse", followEnableW));
			v->addWidget(chkFollow);
		}
		followRow->addWidget(followEnableW, 1);
		followRow->addWidget(mkField("Smoothing Speed", spFollowSpeed), 1);
		followRow->addStretch(1);
		lay->addLayout(followRow);

		
		addSection(lay, "Canvas");

		chkPortraitCover = new QCheckBox(
			"Portrait cover, auto-scale to fill vertical canvases", page);
		chkPortraitCover->setToolTip(
			"When the canvas is taller than wide, scale the capture so it fills"
			" the canvas with no top / bottom gaps.");
		lay->addWidget(chkPortraitCover);

		
		addSection(lay, "Cursor Halo");

		chkShowCursorMarker  = new QCheckBox("Show cursor halo", page);
		chkMarkerOnlyOnClick = new QCheckBox("Show only on click", page);
		chkMarkerOnlyOnClick->setToolTip(
			"When enabled, the halo flashes at the click position"
			" instead of following the cursor continuously.");

		auto *haloFlagsRow = new QHBoxLayout;
		haloFlagsRow->setSpacing(20);
		haloFlagsRow->addWidget(chkShowCursorMarker);
		haloFlagsRow->addWidget(chkMarkerOnlyOnClick);
		haloFlagsRow->addStretch(1);
		lay->addLayout(haloFlagsRow);
		lay->addSpacing(10);

		spMarkerSize = new QSpinBox(page);
		spMarkerSize->setRange(6, 256);
		spMarkerSize->setSingleStep(2);
		spMarkerSize->setSuffix(" px");

		spMarkerThickness = new QSpinBox(page);
		spMarkerThickness->setRange(1, 64);
		spMarkerThickness->setSingleStep(1);
		spMarkerThickness->setSuffix(" px");

		btnMarkerColor = new QPushButton(page);
		btnMarkerColor->setMinimumHeight(28);

		auto *haloRow = new QHBoxLayout;
		haloRow->setSpacing(12);
		haloRow->addWidget(mkField("Halo Size",      spMarkerSize),      1);
		haloRow->addWidget(mkField("Ring Thickness",  spMarkerThickness), 1);
		haloRow->addWidget(mkField("Color",           btnMarkerColor),    1);
		lay->addLayout(haloRow);

		
		addSection(lay, "Developer");

		chkDebug = new QCheckBox("Enable debug logging", page);
		lay->addWidget(chkDebug);

		lay->addStretch(1);
		tabWidget->addTab(page, "Advanced");
	}

	
	
	
	{
		auto *page = new QWidget;
		auto *lay  = new QVBoxLayout(page);
		lay->setContentsMargins(20, 20, 20, 20);
		lay->setSpacing(10);

		auto *info = new QLabel(
			"<b>Check</b> sources to include in the zoom effect.<br>"
			"<b>Uncheck</b> sources that should stay fixed &mdash; "
			"e.g. a camera overlay or a watermark.<br>"
			"The list stays in sync automatically as you add or remove sources.",
			page);
		info->setWordWrap(true);
		lay->addWidget(info);

		lstSources = new QListWidget(page);
		lstSources->setAlternatingRowColors(true);
		lstSources->setSortingEnabled(false); 
		lay->addWidget(lstSources, 1);

		tabWidget->addTab(page, "Sources");
	}

	
	
	
	root->addWidget(tabWidget, 1);

	lblStatus = new QLabel(this);
	lblStatus->setWordWrap(true);
	lblStatus->setText("Tip: Select the monitor that should drive the scene movement.");
	root->addWidget(lblStatus);

	auto *btnRow = new QHBoxLayout;
	btnRow->setSpacing(8);
	btnRefresh = new QPushButton("Refresh Lists", this);
	btnApply   = new QPushButton("Apply", this);
	btnTest    = new QPushButton("Test", this);
	btnRow->addWidget(btnRefresh);
	btnRow->addStretch(1);
	btnRow->addWidget(btnTest);
	btnRow->addWidget(btnApply);
	root->addLayout(btnRow);

	
	connect(btnRefresh, &QPushButton::clicked, this, &ZoominatorDialog::refreshLists);
	connect(btnApply,   &QPushButton::clicked, this, &ZoominatorDialog::applyToController);
	connect(btnTest,    &QPushButton::clicked, this, &ZoominatorDialog::testZoom);
	connect(btnClearHotkey,             &QPushButton::clicked, this, &ZoominatorDialog::clearHotkey);
	connect(btnClearFollowToggleHotkey, &QPushButton::clicked, this, &ZoominatorDialog::clearFollowToggleHotkey);
	connect(btnMarkerColor,             &QPushButton::clicked, this, &ZoominatorDialog::chooseMarkerColor);
}

void ZoominatorDialog::populateSourcesTab()
{
	if (!lstSources)
		return;

	auto &c = ZoominatorController::instance();

	
	
	
	QSet<QString> excluded;
	if (lstSources->count() > 0) {
		for (int i = 0; i < lstSources->count(); i++) {
			const QListWidgetItem *it = lstSources->item(i);
			if (it && it->checkState() == Qt::Unchecked)
				excluded.insert(it->data(Qt::UserRole).toString());
		}
	} else {
		excluded = c.excludedSources;
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
		const QString kind  = friendlySourceKind(sourceMap.value(name));
		const QString label = kind.isEmpty()
		                        ? name
		                        : QStringLiteral("%1  [%2]").arg(name, kind);

		auto *litem = new QListWidgetItem(label, lstSources);
		litem->setData(Qt::UserRole, name);
		litem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		litem->setCheckState(excluded.contains(name) ? Qt::Unchecked : Qt::Checked);
	}
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
		const QString key = QStringLiteral("%1,%2,%3,%4")
		                        .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
		const QString label = QStringLiteral("%1: %2×%3 @ (%4, %5)")
		                          .arg(screen->name())
		                          .arg(g.width()).arg(g.height())
		                          .arg(g.x()).arg(g.y());
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

		chkCtrl->setChecked(c.modCtrl);         chkLeftCtrl->setChecked(c.modLeftCtrl);
		chkAlt->setChecked(c.modAlt);           chkLeftAlt->setChecked(c.modLeftAlt);
		chkShift->setChecked(c.modShift);       chkLeftShift->setChecked(c.modLeftShift);
		chkWin->setChecked(c.modWin);           chkLeftWin->setChecked(c.modLeftWin);
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

	
	c.screenKey   = cmbSource->currentData().toString();
	c.hotkeyMode  = cmbMode->currentData().toString();
	c.followToggleHotkeySequence =
		editFollowToggleHotkey->keySequence().toString(QKeySequence::NativeText);

	
	c.triggerType = cmbTrigger->currentData().toString();
	c.mouseButton = cmbMouseBtn->currentData().toString();

	c.modCtrl  = chkCtrl->isChecked();   c.modLeftCtrl  = chkLeftCtrl->isChecked();
	c.modAlt   = chkAlt->isChecked();    c.modLeftAlt   = chkLeftAlt->isChecked();
	c.modShift = chkShift->isChecked();  c.modLeftShift = chkLeftShift->isChecked();
	c.modWin   = chkWin->isChecked();    c.modLeftWin   = chkLeftWin->isChecked();
	c.modRightCtrl  = chkRightCtrl->isChecked();
	c.modRightAlt   = chkRightAlt->isChecked();
	c.modRightShift = chkRightShift->isChecked();
	c.modRightWin   = chkRightWin->isChecked();

	QKeySequence triggerSequence = editHotkey->keySequence();
	if (!key_sequence_is_modifier_only(triggerSequence) &&
	    (c.modCtrl || c.modAlt || c.modShift || c.modWin ||
	     c.modLeftCtrl || c.modRightCtrl || c.modLeftAlt || c.modRightAlt ||
	     c.modLeftShift || c.modRightShift || c.modLeftWin || c.modRightWin)) {
		triggerSequence = sequence_without_modifiers(triggerSequence);
	}
	c.hotkeySequence = triggerSequence.toString(QKeySequence::NativeText);

	
	c.zoomFactor        = spZoom->value();
	c.animInMs          = spIn->value();
	c.animOutMs         = spOut->value();
	c.followMouse       = chkFollow->isChecked();
	c.followSpeed       = spFollowSpeed->value();
	c.portraitCover     = chkPortraitCover->isChecked();
	c.showCursorMarker  = chkShowCursorMarker->isChecked();
	c.markerOnlyOnClick = chkMarkerOnlyOnClick->isChecked();
	c.markerSize        = spMarkerSize->value();
	c.markerThickness   = spMarkerThickness->value();
	if (btnMarkerColor)
		c.markerColor = (uint32_t)btnMarkerColor->property("markerRgba").toUInt();
	c.debug = chkDebug->isChecked();

	
	c.excludedSources.clear();
	if (lstSources) {
		for (int i = 0; i < lstSources->count(); i++) {
			const QListWidgetItem *it = lstSources->item(i);
			if (it && it->checkState() == Qt::Unchecked)
				c.excludedSources.insert(it->data(Qt::UserRole).toString());
		}
	}

	c.saveSettings();
	lblStatus->setText("Settings applied.");
}

void ZoominatorDialog::testZoom()
{
	applyToController();
	lblStatus->setText("Use your configured trigger to test zoom.");
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
	btnMarkerColor->setText(
		QStringLiteral("%1 / %2 / %3").arg(c.red()).arg(c.green()).arg(c.blue()));
	btnMarkerColor->setStyleSheet(
		QStringLiteral(
			"QPushButton { background:%1; color:%2;"
			" border:1px solid palette(mid); padding:4px 8px; }")
			.arg(c.name(QColor::HexArgb))
			.arg(c.lightness() < 128 ? "#ffffff" : "#000000"));
}

void ZoominatorDialog::chooseMarkerColor()
{
	const uint rgba = btnMarkerColor
	                      ? btnMarkerColor->property("markerRgba").toUInt()
	                      : QColor(255, 0, 0).rgba();
	const QColor picked = QColorDialog::getColor(
		QColor::fromRgba(rgba), this,
		"Pick Cursor Halo Color",
		QColorDialog::ShowAlphaChannel);
	if (picked.isValid())
		updateMarkerColorButton(picked);
}
