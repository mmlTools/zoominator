#include "zoominator-controller.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/platform.h>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QDateTime>
#include <QKeySequence>
#include <QKeyCombination>
#include <QPainter>
#include <QPen>
#include <QRegularExpression>

#include <cmath>
#include <algorithm>

#include <QFileInfo>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#endif

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#endif

#include "zoominator-dialog.hpp"
#include "zoominator-dock.hpp"

/* X11 headers must be included AFTER all Qt headers because Xlib.h defines
   macros (Bool, None, Status, CursorShape, etc.) that clash with Qt
   identifiers. We undefine the worst offenders immediately after. */
#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysymdef.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

/* Undefine X11 macros that collide with Qt identifiers. */
#undef Bool
#undef None
#undef Status
#undef CursorShape
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef Above
#undef Below
#undef Unsorted

/* Re-provide constants we need under safe names. */
static constexpr long X11_None = 0L;
#endif

static int qtKeyToVk(int qtKey);

static constexpr const char *kZoominatorMarkerSourceName = "Zoominator Cursor Marker";

static inline double clampd(double v, double lo, double hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

static inline double smoothstep(double t)
{
	return t * t * (3.0 - 2.0 * t);
}

static inline void logi(bool enabled, const char *fmt, ...)
{
	if (!enabled)
		return;
	va_list args;
	va_start(args, fmt);
	blogva(LOG_INFO, fmt, args);
	va_end(args);
}

ZoominatorController &ZoominatorController::instance()
{
	static ZoominatorController inst;
	return inst;
}

ZoominatorController::ZoominatorController()
{
	tickTimer.setInterval(16);
	connect(&tickTimer, &QTimer::timeout, this, &ZoominatorController::onTick);
}

ZoominatorController::~ZoominatorController()
{
	if (markerSource) {
		obs_source_release(markerSource);
		markerSource = nullptr;
	}
}

QString ZoominatorController::configPath() const
{
	char *path = obs_module_config_path("zoominator.json");
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}

QString ZoominatorController::markerImagePath() const
{
	char *path = obs_module_config_path("zoominator-cursor-marker.png");
	if (!path)
		return {};
	QString p = QString::fromUtf8(path);
	bfree(path);
	return p;
}

static void ensure_parent_dir_exists(const QString &filePath)
{
	if (filePath.isEmpty())
		return;
	QFileInfo fi(filePath);
	const QString dir = fi.absolutePath();
	if (dir.isEmpty())
		return;
	QByteArray dirUtf8 = dir.toUtf8();
	os_mkdirs(dirUtf8.constData());
}

void ZoominatorController::initialize()
{
	loadSettings();
	rebuildTriggersFromSettings();
	installHooks();

	if (!dock) {
		dock = new ZoominatorDock(reinterpret_cast<QWidget *>(obs_frontend_get_main_window()));

#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 0, 0)
		const char *dock_id = "zoominator.dock";
		obs_frontend_add_dock_by_id(dock_id, "Zoominator", dock);
#else
		dock->setWindowTitle("Zoominator");
		dock->setObjectName("zoominator.dock");
		obs_frontend_add_dock(dock);
#endif
	}
}

void ZoominatorController::shutdown()
{
	saveSettings();
#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 0, 0)
	obs_frontend_remove_dock("zoominator.dock");
#endif
	uninstallHooks();
	ensureTicking(false);
	resetState();
	if (markerSource) {
		obs_source_release(markerSource);
		markerSource = nullptr;
	}
}

void ZoominatorController::showDialog()
{
	if (!dialog) {
		dialog = new ZoominatorDialog(reinterpret_cast<QWidget *>(obs_frontend_get_main_window()));
		dialog->setAttribute(Qt::WA_DeleteOnClose, true);
		connect(dialog.data(), &QObject::destroyed, this, [this]() { dialog = nullptr; });
	}
	dialog->show();
	dialog->raise();
	dialog->activateWindow();
}

void ZoominatorController::toggleDockVisibility(bool show)
{
	if (!dock)
		return;
	dock->setVisible(show);
	emit dockVisibilityChanged(show);
}

void ZoominatorController::loadSettings()
{
	sourceName.clear();

	hotkeySequence = QStringLiteral("Ctrl+F1");
	hotkeyMode = QStringLiteral("hold");

	triggerType = QStringLiteral("keyboard");
	mouseButton = QStringLiteral("x1");
	modCtrl = true;
	modAlt = false;
	modShift = false;
	modWin = false;

	zoomFactor = 2.0;
	animInMs = 180;
	animOutMs = 180;
	followMouse = true;
	followMouseRuntimeEnabled = true;
	followSpeed = 8.0;
	portraitCover = true;
	showCursorMarker = false;
	markerOnlyOnClick = false;
	markerColor = 0xFFFF0000;
	markerSize = 26;
	markerThickness = 4;
	debug = false;

	const QString p = configPath();
	if (p.isEmpty())
		return;

	QByteArray pUtf8 = p.toUtf8();
	obs_data_t *data = obs_data_create_from_json_file_safe(pUtf8.constData(), "bak");
	if (!data)
		return;

	auto getStr = [&](const char *key) -> QString {
		const char *v = obs_data_get_string(data, key);
		return v ? QString::fromUtf8(v) : QString();
	};

	sourceName = getStr("source_name");
	hotkeySequence = getStr("hotkey");
	if (hotkeySequence.isEmpty())
		hotkeySequence = QStringLiteral("Ctrl+F1");
	hotkeyMode = getStr("hotkey_mode");
	followToggleHotkeySequence = getStr("follow_toggle_hotkey");
	if (hotkeyMode != "toggle")
		hotkeyMode = "hold";

	triggerType = getStr("trigger_type");
	QKeySequence followSeq(followToggleHotkeySequence);
	if (!followSeq.isEmpty()) {
		const QKeyCombination kc = followSeq[0];
		const auto mods = kc.keyboardModifiers();
		const int key = int(kc.key());
		followToggleHotkeyVk = qtKeyToVk(key);
		followToggleModCtrl = mods.testFlag(Qt::ControlModifier);
		followToggleModAlt = mods.testFlag(Qt::AltModifier);
		followToggleModShift = mods.testFlag(Qt::ShiftModifier);
		followToggleModWin = mods.testFlag(Qt::MetaModifier);

#ifdef _WIN32
		const bool keyIsModifier = (followToggleHotkeyVk == VK_CONTROL || followToggleHotkeyVk == VK_LCONTROL ||
					    followToggleHotkeyVk == VK_RCONTROL || followToggleHotkeyVk == VK_MENU ||
					    followToggleHotkeyVk == VK_LMENU || followToggleHotkeyVk == VK_RMENU ||
					    followToggleHotkeyVk == VK_SHIFT || followToggleHotkeyVk == VK_LSHIFT ||
					    followToggleHotkeyVk == VK_RSHIFT || followToggleHotkeyVk == VK_LWIN ||
					    followToggleHotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
		const bool keyIsModifier =
			(followToggleHotkeyVk == kVK_Control || followToggleHotkeyVk == kVK_RightControl ||
			 followToggleHotkeyVk == kVK_Option || followToggleHotkeyVk == kVK_RightOption ||
			 followToggleHotkeyVk == kVK_Shift || followToggleHotkeyVk == kVK_RightShift ||
			 followToggleHotkeyVk == kVK_Command || followToggleHotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
		const bool keyIsModifier =
			(followToggleHotkeyVk == XK_Control_L || followToggleHotkeyVk == XK_Control_R ||
			 followToggleHotkeyVk == XK_Alt_L || followToggleHotkeyVk == XK_Alt_R ||
			 followToggleHotkeyVk == XK_Shift_L || followToggleHotkeyVk == XK_Shift_R ||
			 followToggleHotkeyVk == XK_Super_L || followToggleHotkeyVk == XK_Super_R);
#else
		const bool keyIsModifier = false;
#endif

		if (keyIsModifier && mods == Qt::NoModifier) {
#ifdef _WIN32
			followToggleModCtrl = (followToggleHotkeyVk == VK_CONTROL ||
					       followToggleHotkeyVk == VK_LCONTROL ||
					       followToggleHotkeyVk == VK_RCONTROL);
			followToggleModAlt = (followToggleHotkeyVk == VK_MENU || followToggleHotkeyVk == VK_LMENU ||
					      followToggleHotkeyVk == VK_RMENU);
			followToggleModShift = (followToggleHotkeyVk == VK_SHIFT || followToggleHotkeyVk == VK_LSHIFT ||
						followToggleHotkeyVk == VK_RSHIFT);
			followToggleModWin = (followToggleHotkeyVk == VK_LWIN || followToggleHotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
			followToggleModCtrl =
				(followToggleHotkeyVk == kVK_Control || followToggleHotkeyVk == kVK_RightControl);
			followToggleModAlt =
				(followToggleHotkeyVk == kVK_Option || followToggleHotkeyVk == kVK_RightOption);
			followToggleModShift =
				(followToggleHotkeyVk == kVK_Shift || followToggleHotkeyVk == kVK_RightShift);
			followToggleModWin =
				(followToggleHotkeyVk == kVK_Command || followToggleHotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
			followToggleModCtrl =
				(followToggleHotkeyVk == XK_Control_L || followToggleHotkeyVk == XK_Control_R);
			followToggleModAlt =
				(followToggleHotkeyVk == XK_Alt_L || followToggleHotkeyVk == XK_Alt_R);
			followToggleModShift =
				(followToggleHotkeyVk == XK_Shift_L || followToggleHotkeyVk == XK_Shift_R);
			followToggleModWin =
				(followToggleHotkeyVk == XK_Super_L || followToggleHotkeyVk == XK_Super_R);
#else
			followToggleHotkeyVk = 0;
#endif
		}

		followToggleHkValid = (followToggleHotkeyVk != 0) &&
				      (followToggleModCtrl || followToggleModAlt || followToggleModShift ||
				       followToggleModWin || key != 0);
	}

	if (triggerType != "mouse")
		triggerType = "keyboard";

	mouseButton = getStr("mouse_button");
	if (mouseButton.isEmpty())
		mouseButton = "x1";

	if (obs_data_has_user_value(data, "mod_ctrl"))
		modCtrl = obs_data_get_bool(data, "mod_ctrl");
	if (obs_data_has_user_value(data, "mod_alt"))
		modAlt = obs_data_get_bool(data, "mod_alt");
	if (obs_data_has_user_value(data, "mod_shift"))
		modShift = obs_data_get_bool(data, "mod_shift");
	if (obs_data_has_user_value(data, "mod_win"))
		modWin = obs_data_get_bool(data, "mod_win");

	if (obs_data_has_user_value(data, "zoom_factor"))
		zoomFactor = obs_data_get_double(data, "zoom_factor");

	if (zoomFactor < 0.0)
		zoomFactor = 0.0;

	if (obs_data_has_user_value(data, "anim_in_ms"))
		animInMs = (int)obs_data_get_int(data, "anim_in_ms");
	if (obs_data_has_user_value(data, "anim_out_ms"))
		animOutMs = (int)obs_data_get_int(data, "anim_out_ms");
	if (animInMs < 0)
		animInMs = 180;
	if (animOutMs < 0)
		animOutMs = 180;

	if (obs_data_has_user_value(data, "follow_mouse"))
		followMouse = obs_data_get_bool(data, "follow_mouse");
	followMouseRuntimeEnabled = true;
	if (obs_data_has_user_value(data, "follow_speed"))
		followSpeed = obs_data_get_double(data, "follow_speed");
	if (followSpeed <= 0.1)
		followSpeed = 8.0;

	if (obs_data_has_user_value(data, "portrait_cover"))
		portraitCover = obs_data_get_bool(data, "portrait_cover");
	if (obs_data_has_user_value(data, "show_cursor_marker"))
		showCursorMarker = obs_data_get_bool(data, "show_cursor_marker");
	if (obs_data_has_user_value(data, "marker_only_on_click"))
		markerOnlyOnClick = obs_data_get_bool(data, "marker_only_on_click");
	if (obs_data_has_user_value(data, "marker_color"))
		markerColor = (uint32_t)obs_data_get_int(data, "marker_color");
	if (obs_data_has_user_value(data, "marker_size"))
		markerSize = (int)obs_data_get_int(data, "marker_size");
	if (obs_data_has_user_value(data, "marker_thickness"))
		markerThickness = (int)obs_data_get_int(data, "marker_thickness");
	if (markerSize < 6)
		markerSize = 6;
	if (markerSize > 256)
		markerSize = 256;
	if (markerThickness < 1)
		markerThickness = 1;
	if (markerThickness > 64)
		markerThickness = 64;

	if (obs_data_has_user_value(data, "debug"))
		debug = obs_data_get_bool(data, "debug");

	obs_data_release(data);

	logi(debug, "[Zoominator] Loaded settings from: %s", pUtf8.constData());

	rebuildTriggersFromSettings();
	emit settingsChanged();
}

void ZoominatorController::notifySettingsChanged()
{
	emit settingsChanged();
}

void ZoominatorController::saveSettings()
{
	const QString p = configPath();
	if (p.isEmpty())
		return;

	ensure_parent_dir_exists(p);

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "source_name", sourceName.toUtf8().constData());
	obs_data_set_string(data, "hotkey", hotkeySequence.toUtf8().constData());
	obs_data_set_string(data, "hotkey_mode", hotkeyMode.toUtf8().constData());
	obs_data_set_string(data, "follow_toggle_hotkey", followToggleHotkeySequence.toUtf8().constData());

	obs_data_set_string(data, "trigger_type", triggerType.toUtf8().constData());
	obs_data_set_string(data, "mouse_button", mouseButton.toUtf8().constData());
	obs_data_set_bool(data, "mod_ctrl", modCtrl);
	obs_data_set_bool(data, "mod_alt", modAlt);
	obs_data_set_bool(data, "mod_shift", modShift);
	obs_data_set_bool(data, "mod_win", modWin);

	obs_data_set_double(data, "zoom_factor", zoomFactor);
	obs_data_set_int(data, "anim_in_ms", animInMs);
	obs_data_set_int(data, "anim_out_ms", animOutMs);
	obs_data_set_bool(data, "follow_mouse", followMouse);
	followMouseRuntimeEnabled = true;
	obs_data_set_double(data, "follow_speed", followSpeed);
	obs_data_set_bool(data, "portrait_cover", portraitCover);
	obs_data_set_bool(data, "show_cursor_marker", showCursorMarker);
	obs_data_set_bool(data, "marker_only_on_click", markerOnlyOnClick);
	obs_data_set_int(data, "marker_color", (long long)markerColor);
	obs_data_set_int(data, "marker_size", markerSize);
	obs_data_set_int(data, "marker_thickness", markerThickness);
	obs_data_set_bool(data, "debug", debug);

	QByteArray pUtf8 = p.toUtf8();
	obs_data_save_json_safe(data, pUtf8.constData(), "tmp", "bak");
	obs_data_release(data);

	logi(debug, "[Zoominator] Saved settings to: %s", pUtf8.constData());

	rebuildTriggersFromSettings();
	emit settingsChanged();
}

void ZoominatorController::ensureTicking(bool on)
{
	if (on) {
		if (!tickTimer.isActive())
			tickTimer.start();
	} else {
		if (tickTimer.isActive())
			tickTimer.stop();
	}
}

void ZoominatorController::startZoomIn()
{
	animDir = +1;
	ensureTicking(true);
}

void ZoominatorController::startZoomOut()
{
	animDir = -1;
	ensureTicking(true);
}

void ZoominatorController::resetState()
{
	zoomPressed = false;
	zoomLatched = false;
	zoomActive = false;
	animT = 0.0;
	animDir = 0;
	followHasPos = false;
	orig.valid = false;
	markerRenderedOpacity = -1;
	markerClickFlashStartMs = 0;
	markerClickFlashHoldUntilMs = 0;
	markerClickFlashFadeOutEndMs = 0;

	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (sceneSource) {
		obs_scene_t *scene = obs_scene_from_source(sceneSource);
		if (scene)
			hideMarkerInScene(scene);
		obs_source_release(sceneSource);
	}
}

obs_sceneitem_t *ZoominatorController::findTargetItemInCurrentScene() const
{
	if (sourceName.isEmpty())
		return nullptr;

	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	if (!sceneSource)
		return nullptr;

	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	obs_source_release(sceneSource);
	if (!scene)
		return nullptr;

	struct Finder {
		QString want;
		obs_sceneitem_t *found = nullptr;

		static bool enum_cb(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *f = static_cast<Finder *>(param);
			if (!f || f->found)
				return false;
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (!src)
				return true;
			const char *nm = obs_source_get_name(src);
			if (nm && *nm && f->want == QString::fromUtf8(nm)) {
				f->found = item;
				return false;
			}
			return true;
		}
	};

	Finder f{sourceName, nullptr};
	obs_scene_enum_items(scene, Finder::enum_cb, &f);
	return f.found;
}

bool ZoominatorController::getCursorPos(int &x, int &y) const
{
#ifdef _WIN32
	POINT p{};
	if (!::GetCursorPos(&p))
		return false;
	x = (int)p.x;
	y = (int)p.y;
	return true;
#elif defined(__APPLE__)
	CGEventRef event = CGEventCreate(NULL);
	if (!event)
		return false;
	CGPoint loc = CGEventGetLocation(event);
	CFRelease(event);
	x = (int)loc.x;
	y = (int)loc.y;
	return true;
#elif defined(__linux__)
	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy)
		return false;
	Window root = DefaultRootWindow(dpy);
	Window retRoot = 0, retChild = 0;
	int rootX = 0, rootY = 0, winX = 0, winY = 0;
	unsigned int mask = 0;
	int ok = XQueryPointer(dpy, root, &retRoot, &retChild, &rootX, &rootY, &winX, &winY, &mask);
	XCloseDisplay(dpy);
	(void)retRoot; (void)retChild; (void)winX; (void)winY; (void)mask;
	if (!ok)
		return false;
	x = rootX;
	y = rootY;
	return true;
#else
	(void)x;
	(void)y;
	return false;
#endif
}

static bool get_monitor_capture_selector(obs_source_t *src, QString &selector, int &monitorId, bool &hasId)
{
	selector.clear();
	monitorId = -1;
	hasId = false;

	if (!src)
		return false;

	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;

	auto pick_str = [&](const char *key) -> const char * {
		const char *v = obs_data_get_string(s, key);
		return (v && *v) ? v : nullptr;
	};

	if (const char *v = pick_str("alt_id"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("monitor_device"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("display_device"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("device"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("monitor"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("monitor_id"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("id"))
		selector = QString::fromUtf8(v);
	else if (const char *v = pick_str("setting_id"))
		selector = QString::fromUtf8(v);

	if (selector.isEmpty()) {
		monitorId = (int)obs_data_get_int(s, "monitor_id");
		if (monitorId != 0)
			hasId = true;

		if (!hasId) {
			monitorId = (int)obs_data_get_int(s, "monitor");
			if (monitorId != 0)
				hasId = true;
		}
		if (!hasId) {
			monitorId = (int)obs_data_get_int(s, "display");
			if (monitorId != 0)
				hasId = true;
		}
		if (!hasId) {
			monitorId = (int)obs_data_get_int(s, "screen");
			if (monitorId != 0)
				hasId = true;
		}
	}

	obs_data_release(s);
	return (!selector.isEmpty()) || hasId;
}

#ifdef _WIN32
struct MonitorInfoLite {
	QString device;
	RECT rc{};
};

static std::vector<MonitorInfoLite> enum_monitors()
{
	std::vector<MonitorInfoLite> out;

	auto cb = [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
		auto *vec = reinterpret_cast<std::vector<MonitorInfoLite> *>(lp);
		MONITORINFOEXA mi{};
		mi.cbSize = sizeof(mi);
		if (!GetMonitorInfoA(hMon, &mi))
			return TRUE;
		MonitorInfoLite m;
		m.device = QString::fromLatin1(mi.szDevice);
		m.rc = mi.rcMonitor;
		vec->push_back(m);
		return TRUE;
	};

	EnumDisplayMonitors(nullptr, nullptr, cb, reinterpret_cast<LPARAM>(&out));
	return out;
}

#ifdef _WIN32
#include <vector>
#include <string>
#include <cwctype>
#include <windows.h>
#include <winuser.h>
#include <wingdi.h>

static bool wcs_ieq(const wchar_t *a, const wchar_t *b)
{
	if (!a || !b)
		return false;
	while (*a && *b) {
		if (towlower(*a) != towlower(*b))
			return false;
		++a;
		++b;
	}
	return *a == 0 && *b == 0;
}

static bool resolve_displayconfig_path_to_gdi(const QString &selector, QString &outGdi)
{
	outGdi.clear();
	if (selector.isEmpty())
		return false;

	const std::wstring want = selector.toStdWString();
	if (want.rfind(LR"(\\?\DISPLAY#)", 0) != 0)
		return false;

	UINT32 pathCount = 0, modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
		return false;

	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);

	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
	    ERROR_SUCCESS)
		return false;

	for (UINT32 i = 0; i < pathCount; ++i) {
		const auto &p = paths[i];

		DISPLAYCONFIG_TARGET_DEVICE_NAME tdn{};
		tdn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
		tdn.header.size = sizeof(tdn);
		tdn.header.adapterId = p.targetInfo.adapterId;
		tdn.header.id = p.targetInfo.id;

		if (DisplayConfigGetDeviceInfo(&tdn.header) != ERROR_SUCCESS)
			continue;

		if (!wcs_ieq(tdn.monitorDevicePath, want.c_str()))
			continue;

		DISPLAYCONFIG_SOURCE_DEVICE_NAME sdn{};
		sdn.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
		sdn.header.size = sizeof(sdn);
		sdn.header.adapterId = p.sourceInfo.adapterId;
		sdn.header.id = p.sourceInfo.id;

		if (DisplayConfigGetDeviceInfo(&sdn.header) != ERROR_SUCCESS)
			return false;

		outGdi = QString::fromWCharArray(sdn.viewGdiDeviceName);
		return !outGdi.isEmpty();
	}

	return false;
}
#endif

static bool match_monitor_rect(obs_source_t *src, RECT &rcOut)
{
	QString selector;
	int monId = -1;
	bool hasId = false;
	if (!get_monitor_capture_selector(src, selector, monId, hasId))
		return false;

#ifdef _WIN32
	if (!selector.isEmpty()) {
		QString gdi;
		if (resolve_displayconfig_path_to_gdi(selector, gdi) && !gdi.isEmpty()) {
			selector = gdi;
		}
	}
#endif

	auto mons = enum_monitors();
	if (mons.empty())
		return false;

	if (!selector.isEmpty()) {
		for (auto &m : mons) {
			if (m.device == selector) {
				rcOut = m.rc;
				return true;
			}
			QRegularExpression re("DISPLAY(\\d+)");
			auto mw = re.match(selector);
			auto md = re.match(m.device);
			if (mw.hasMatch() && md.hasMatch() && mw.captured(1) == md.captured(1)) {
				rcOut = m.rc;
				return true;
			}
		}
	}

	if (hasId) {
		int idx = monId;
		if (idx >= 1 && idx <= (int)mons.size() && !selector.isEmpty() == false) {
		}
		if (idx >= 0 && idx < (int)mons.size()) {
			rcOut = mons[(size_t)idx].rc;
			return true;
		}
		if (idx >= 1 && idx <= (int)mons.size()) {
			rcOut = mons[(size_t)(idx - 1)].rc;
			return true;
		}
	}

	return false;
}

static bool parse_obs_window_selector(const QString &sel, QString &title, QString &clazz, QString &exe)
{
	title.clear();
	clazz.clear();
	exe.clear();
	if (sel.isEmpty())
		return false;

	QString s = sel;
	int last = s.lastIndexOf(':');
	if (last <= 0) {
		title = s;
		return true;
	}
	exe = s.mid(last + 1);
	s = s.left(last);

	int mid = s.lastIndexOf(':');
	if (mid <= 0) {
		title = s;
		return true;
	}
	clazz = s.mid(mid + 1);
	title = s.left(mid);
	return true;
}

static std::wstring to_w(const QString &s)
{
	return s.toStdWString();
}

static bool get_process_exe_name(DWORD pid, QString &out)
{
	out.clear();
	HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!h)
		return false;
	wchar_t buf[MAX_PATH]{};
	if (GetModuleFileNameExW(h, nullptr, buf, MAX_PATH) == 0) {
		CloseHandle(h);
		return false;
	}
	CloseHandle(h);
	QString full = QString::fromWCharArray(buf);
	int slash = std::max(full.lastIndexOf('\\'), full.lastIndexOf('/'));
	out = (slash >= 0) ? full.mid(slash + 1) : full;
	return true;
}

struct WinFindCtx {
	QString wantTitle;
	QString wantClass;
	QString wantExe;
	HWND found = nullptr;
};

static BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lp)
{
	auto *ctx = reinterpret_cast<WinFindCtx *>(lp);
	if (!ctx)
		return TRUE;
	if (!IsWindowVisible(hwnd))
		return TRUE;

	wchar_t titleBuf[512]{};
	GetWindowTextW(hwnd, titleBuf, 512);
	QString title = QString::fromWCharArray(titleBuf);

	wchar_t classBuf[256]{};
	GetClassNameW(hwnd, classBuf, 256);
	QString clazz = QString::fromWCharArray(classBuf);

	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	QString exe;
	if (pid)
		get_process_exe_name(pid, exe);

	auto norm = [](QString s) {
		s = s.trimmed();
		return s;
	};

	const QString wt = norm(ctx->wantTitle);
	const QString wc = norm(ctx->wantClass);
	const QString we = norm(ctx->wantExe);

	bool ok = true;
	if (!we.isEmpty())
		ok = ok && exe.compare(we, Qt::CaseInsensitive) == 0;
	if (!wc.isEmpty())
		ok = ok && clazz.compare(wc, Qt::CaseInsensitive) == 0;

	if (!wt.isEmpty()) {
		ok = ok && title.contains(wt, Qt::CaseInsensitive);
	}

	if (ok) {
		ctx->found = hwnd;
		return FALSE;
	}
	return TRUE;
}

static bool match_window_rect_for_source(obs_source_t *src, RECT &rcOut)
{
	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;
	const char *w = obs_data_get_string(s, "window");
	QString sel = (w && *w) ? QString::fromUtf8(w) : QString();
	obs_data_release(s);

	if (sel.isEmpty())
		return false;

	QString title, clazz, exe;
	parse_obs_window_selector(sel, title, clazz, exe);

	WinFindCtx ctx;
	ctx.wantTitle = title;
	ctx.wantClass = clazz;
	ctx.wantExe = exe;

	EnumWindows(enum_windows_cb, reinterpret_cast<LPARAM>(&ctx));
	if (!ctx.found)
		return false;

	RECT rc{};
	if (!GetWindowRect(ctx.found, &rc))
		return false;

	rcOut = rc;
	return true;
}
#endif // _WIN32

#ifdef __APPLE__
struct MonitorInfoLite {
	CGDirectDisplayID displayID;
	CGRect rc;
};

static std::vector<MonitorInfoLite> enum_monitors()
{
	std::vector<MonitorInfoLite> out;
	uint32_t count = 0;
	CGGetActiveDisplayList(0, nullptr, &count);
	if (count == 0)
		return out;
	std::vector<CGDirectDisplayID> displays(count);
	CGGetActiveDisplayList(count, displays.data(), &count);
	for (uint32_t i = 0; i < count; i++) {
		MonitorInfoLite m;
		m.displayID = displays[i];
		m.rc = CGDisplayBounds(displays[i]);
		out.push_back(m);
	}
	return out;
}

static bool match_monitor_rect(obs_source_t *src, CGRect &rcOut)
{
	auto mons = enum_monitors();
	if (mons.empty())
		return false;

	obs_data_t *s = obs_source_get_settings(src);
	if (s) {
		const char *uuid = obs_data_get_string(s, "display_uuid");
		if (uuid && *uuid) {
			for (auto &m : mons) {
				CFUUIDRef duuid = CGDisplayCreateUUIDFromDisplayID(m.displayID);
				if (duuid) {
					CFStringRef uuidStr = CFUUIDCreateString(kCFAllocatorDefault, duuid);
					CFRelease(duuid);
					if (uuidStr) {
						char buf[128];
						if (CFStringGetCString(uuidStr, buf, sizeof(buf), kCFStringEncodingUTF8)) {
							if (strcasecmp(buf, uuid) == 0) {
								CFRelease(uuidStr);
								obs_data_release(s);
								rcOut = m.rc;
								return true;
							}
						}
						CFRelease(uuidStr);
					}
				}
			}
		}

		if (obs_data_has_user_value(s, "display")) {
			int idx = (int)obs_data_get_int(s, "display");
			if (idx >= 0 && idx < (int)mons.size()) {
				obs_data_release(s);
				rcOut = mons[(size_t)idx].rc;
				return true;
			}
		}

		obs_data_release(s);
	}

	QString selector;
	int monId = -1;
	bool hasId = false;
	if (get_monitor_capture_selector(src, selector, monId, hasId) && hasId) {
		CGDirectDisplayID wantId = (CGDirectDisplayID)monId;
		for (auto &m : mons) {
			if (m.displayID == wantId) {
				rcOut = m.rc;
				return true;
			}
		}
	}

	if (mons.size() == 1) {
		rcOut = mons[0].rc;
		return true;
	}

	return false;
}

static bool match_window_rect_for_source(obs_source_t *src, CGRect &rcOut)
{
	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;

	int64_t windowID = obs_data_get_int(s, "window");
	if (windowID == 0)
		windowID = obs_data_get_int(s, "window_id");
	const char *ownerName = obs_data_get_string(s, "owner_name");
	obs_data_release(s);

	CFArrayRef windowList = CGWindowListCopyWindowInfo(
		kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
	if (!windowList)
		return false;

	bool found = false;
	CFIndex count = CFArrayGetCount(windowList);
	for (CFIndex i = 0; i < count && !found; i++) {
		CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(windowList, i);

		if (windowID != 0) {
			CFNumberRef numRef = (CFNumberRef)CFDictionaryGetValue(dict, kCGWindowNumber);
			if (numRef) {
				int64_t wid = 0;
				CFNumberGetValue(numRef, kCFNumberSInt64Type, &wid);
				if (wid == windowID) {
					CFDictionaryRef boundsDict =
						(CFDictionaryRef)CFDictionaryGetValue(dict, kCGWindowBounds);
					if (boundsDict && CGRectMakeWithDictionaryRepresentation(boundsDict, &rcOut))
						found = true;
				}
			}
			continue;
		}

		if (ownerName && *ownerName) {
			CFStringRef owner = (CFStringRef)CFDictionaryGetValue(dict, kCGWindowOwnerName);
			if (owner) {
				char buf[256];
				if (CFStringGetCString(owner, buf, sizeof(buf), kCFStringEncodingUTF8)) {
					if (strcmp(buf, ownerName) == 0) {
						CFDictionaryRef boundsDict =
							(CFDictionaryRef)CFDictionaryGetValue(dict, kCGWindowBounds);
						if (boundsDict &&
						    CGRectMakeWithDictionaryRepresentation(boundsDict, &rcOut))
							found = true;
					}
				}
			}
		}
	}

	CFRelease(windowList);
	return found;
}
#endif // __APPLE__

#ifdef __linux__
struct MonitorInfoLite {
	QString name;
	int x, y, w, h;
};

static std::vector<MonitorInfoLite> enum_monitors()
{
	std::vector<MonitorInfoLite> out;
	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy)
		return out;

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	XRRScreenResources *res = XRRGetScreenResources(dpy, root);
	if (!res) {
		XCloseDisplay(dpy);
		return out;
	}

	for (int i = 0; i < res->noutput; i++) {
		XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (!oi)
			continue;
		if (oi->connection != RR_Connected || oi->crtc == X11_None) {
			XRRFreeOutputInfo(oi);
			continue;
		}
		XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
		if (!ci) {
			XRRFreeOutputInfo(oi);
			continue;
		}
		MonitorInfoLite m;
		m.name = QString::fromLatin1(oi->name);
		m.x = (int)ci->x;
		m.y = (int)ci->y;
		m.w = (int)ci->width;
		m.h = (int)ci->height;
		out.push_back(m);
		XRRFreeCrtcInfo(ci);
		XRRFreeOutputInfo(oi);
	}

	XRRFreeScreenResources(res);
	XCloseDisplay(dpy);
	return out;
}

struct LinuxRect {
	int x, y, w, h;
};

static bool match_monitor_rect(obs_source_t *src, LinuxRect &rcOut)
{
	auto mons = enum_monitors();
	if (mons.empty())
		return false;

	QString selector;
	int monId = -1;
	bool hasId = false;
	get_monitor_capture_selector(src, selector, monId, hasId);

	/* OBS on Linux (PipeWire/X11) often stores monitor index as an integer. */
	if (!selector.isEmpty()) {
		for (auto &m : mons) {
			if (m.name == selector) {
				rcOut = {m.x, m.y, m.w, m.h};
				return true;
			}
		}
		/* Try matching "DISPLAY<N>" pattern, or just the trailing number. */
		QRegularExpression re("(\\d+)$");
		auto mw = re.match(selector);
		if (mw.hasMatch()) {
			int idx = mw.captured(1).toInt();
			if (idx >= 0 && idx < (int)mons.size()) {
				auto &m = mons[(size_t)idx];
				rcOut = {m.x, m.y, m.w, m.h};
				return true;
			}
		}
	}

	if (hasId) {
		if (monId >= 0 && monId < (int)mons.size()) {
			auto &m = mons[(size_t)monId];
			rcOut = {m.x, m.y, m.w, m.h};
			return true;
		}
		if (monId >= 1 && monId <= (int)mons.size()) {
			auto &m = mons[(size_t)(monId - 1)];
			rcOut = {m.x, m.y, m.w, m.h};
			return true;
		}
	}

	/* Fallback: if only one monitor, use it. */
	if (mons.size() == 1) {
		auto &m = mons[0];
		rcOut = {m.x, m.y, m.w, m.h};
		return true;
	}

	return false;
}

static bool parse_obs_window_selector_linux(const QString &sel, QString &title, QString &clazz, QString &exe)
{
	title.clear();
	clazz.clear();
	exe.clear();
	if (sel.isEmpty())
		return false;

	/* OBS Linux window capture uses "title:class:exe" format. */
	QString s = sel;
	int last = s.lastIndexOf(':');
	if (last <= 0) {
		title = s;
		return true;
	}
	exe = s.mid(last + 1);
	s = s.left(last);

	int mid = s.lastIndexOf(':');
	if (mid <= 0) {
		title = s;
		return true;
	}
	clazz = s.mid(mid + 1);
	title = s.left(mid);
	return true;
}

static bool match_window_rect_for_source(obs_source_t *src, LinuxRect &rcOut)
{
	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;

	/* OBS xcomposite/xshm window capture stores "window" or "capture_window" as XID or selector. */
	const char *w = obs_data_get_string(s, "window");
	QString sel = (w && *w) ? QString::fromUtf8(w) : QString();
	if (sel.isEmpty()) {
		w = obs_data_get_string(s, "capture_window");
		sel = (w && *w) ? QString::fromUtf8(w) : QString();
	}

	/* Also try the window name property. */
	const char *windowName = obs_data_get_string(s, "window_name");
	QString wantName = (windowName && *windowName) ? QString::fromUtf8(windowName) : QString();

	int64_t windowId = obs_data_get_int(s, "window");
	obs_data_release(s);

	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy)
		return false;

	/* If we have a numeric window ID, use it directly. */
	if (windowId > 0) {
		XWindowAttributes attr;
		if (XGetWindowAttributes(dpy, (Window)windowId, &attr)) {
			int absX = 0, absY = 0;
			Window child;
			XTranslateCoordinates(dpy, (Window)windowId, DefaultRootWindow(dpy), 0, 0, &absX, &absY,
					      &child);
			XCloseDisplay(dpy);
			rcOut = {absX, absY, attr.width, attr.height};
			return true;
		}
	}

	/* Parse the selector string for title/class matching. */
	QString title, clazz, exe;
	if (!sel.isEmpty())
		parse_obs_window_selector_linux(sel, title, clazz, exe);
	if (title.isEmpty() && !wantName.isEmpty())
		title = wantName;

	if (title.isEmpty() && clazz.isEmpty()) {
		XCloseDisplay(dpy);
		return false;
	}

	/* Enumerate top-level windows. */
	Window root = DefaultRootWindow(dpy);
	Atom netClientList = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
	bool found = false;

	if (netClientList != X11_None) {
		Atom type;
		int format;
		unsigned long nitems, bytesAfter;
		unsigned char *data = nullptr;
		if (XGetWindowProperty(dpy, root, netClientList, 0, ~0L, False, XA_WINDOW, &type, &format, &nitems,
				       &bytesAfter, &data) == Success &&
		    data) {
			Window *windows = (Window *)data;
			for (unsigned long i = 0; i < nitems && !found; i++) {
				Window win = windows[i];

				/* Check class. */
				if (!clazz.isEmpty()) {
					XClassHint ch{};
					if (XGetClassHint(dpy, win, &ch)) {
						QString resName = ch.res_name ? QString::fromUtf8(ch.res_name) : QString();
						QString resClass = ch.res_class ? QString::fromUtf8(ch.res_class) : QString();
						if (ch.res_name)
							XFree(ch.res_name);
						if (ch.res_class)
							XFree(ch.res_class);
						if (resName.compare(clazz, Qt::CaseInsensitive) != 0 &&
						    resClass.compare(clazz, Qt::CaseInsensitive) != 0)
							continue;
					} else {
						continue;
					}
				}

				/* Check title. */
				if (!title.isEmpty()) {
					char *name = nullptr;
					if (XFetchName(dpy, win, &name) && name) {
						QString winTitle = QString::fromUtf8(name);
						XFree(name);
						if (!winTitle.contains(title, Qt::CaseInsensitive))
							continue;
					} else {
						continue;
					}
				}

				/* Found a match -- get geometry. */
				XWindowAttributes attr;
				if (XGetWindowAttributes(dpy, win, &attr)) {
					int absX = 0, absY = 0;
					Window child;
					XTranslateCoordinates(dpy, win, root, 0, 0, &absX, &absY, &child);
					rcOut = {absX, absY, attr.width, attr.height};
					found = true;
				}
			}
			XFree(data);
		}
	}

	XCloseDisplay(dpy);
	return found;
}
#endif // __linux__

bool ZoominatorController::mapCursorToSourcePixels(obs_source_t *src, int cursorX, int cursorY, float &sx, float &sy,
						   bool &cursorInside) const
{
	cursorInside = false;
	sx = 0.f;
	sy = 0.f;
	if (!src)
		return false;

	const char *id = obs_source_get_id(src);
	if (!id)
		return false;

#ifdef _WIN32
	RECT rc{};
	bool haveRect = false;
	const bool isMonitorCap = (strcmp(id, "monitor_capture") == 0 || strcmp(id, "display_capture") == 0 ||
				   strcmp(id, "screen_capture") == 0 ||
				   (strstr(id, "monitor") && strstr(id, "capture")));

	if (isMonitorCap) {
		haveRect = match_monitor_rect(src, rc);
	} else if (strcmp(id, "window_capture") == 0 || strcmp(id, "game_capture") == 0) {
		haveRect = match_window_rect_for_source(src, rc);
		if (!haveRect) {
			POINT pt{cursorX, cursorY};
			HWND h = WindowFromPoint(pt);
			if (h) {
				HWND root = GetAncestor(h, GA_ROOT);
				if (root)
					h = root;
				RECT trc{};
				if (GetWindowRect(h, &trc)) {
					if ((trc.right - trc.left) > 64 && (trc.bottom - trc.top) > 64) {
						rc = trc;
						haveRect = true;
					}
				}
			}
		}
	}

	if (!haveRect) {
		if (isMonitorCap)
			return false;

		const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
		if (vw > 0 && vh > 0) {
			rc.left = vx;
			rc.top = vy;
			rc.right = vx + vw;
			rc.bottom = vy + vh;
			haveRect = true;
		} else {
			return false;
		}
	}

	const int w = rc.right - rc.left;
	const int h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0)
		return false;

	cursorInside = !(cursorX < rc.left || cursorX >= rc.right || cursorY < rc.top || cursorY >= rc.bottom);

	const int clampedX = (cursorX < rc.left) ? rc.left : (cursorX >= rc.right ? (rc.right - 1) : cursorX);
	const int clampedY = (cursorY < rc.top) ? rc.top : (cursorY >= rc.bottom ? (rc.bottom - 1) : cursorY);

	const double relX = (clampedX - rc.left) / (double)w;
	const double relY = (clampedY - rc.top) / (double)h;

const uint32_t sw = obs_source_get_width(src);
const uint32_t sh = obs_source_get_height(src);
if (sw == 0 || sh == 0)
	return false;

sx = (float)(relX * (double)sw);
sy = (float)(relY * (double)sh);
return true;
#elif defined(__APPLE__)
	CGRect rc;
	bool haveRect = false;
	
	const bool isUnifiedScreenCap = (strcmp(id, "screen_capture") == 0 || strcmp(id, "macos_screen_capture") == 0);
	int captureType = -1;
	if (isUnifiedScreenCap) {
		obs_data_t *props = obs_source_get_settings(src);
		if (props) {
			captureType = (int)obs_data_get_int(props, "type");
			obs_data_release(props);
		}
	}

	const bool isDisplayCap = (isUnifiedScreenCap && captureType == 0) || strcmp(id, "display_capture") == 0 ||
				  strcmp(id, "monitor_capture") == 0;
	const bool isWindowCap = (isUnifiedScreenCap && captureType == 1) || strcmp(id, "window_capture") == 0;

	if (isDisplayCap) {
		haveRect = match_monitor_rect(src, rc);
	} else if (isWindowCap || (isUnifiedScreenCap && captureType >= 1)) {
		haveRect = match_window_rect_for_source(src, rc);
	}

	if (!haveRect) {
		if (isDisplayCap)
			return false;
		rc = CGDisplayBounds(CGMainDisplayID());
		haveRect = (rc.size.width > 0 && rc.size.height > 0);
		if (!haveRect)
			return false;
	}

	const int w = (int)rc.size.width;
	const int h = (int)rc.size.height;
	if (w <= 0 || h <= 0)
		return false;

	cursorInside = CGRectContainsPoint(rc, CGPointMake(cursorX, cursorY));

	const int rcX = (int)rc.origin.x;
	const int rcY = (int)rc.origin.y;
	const int clampedX = std::max(rcX, std::min(cursorX, rcX + w - 1));
	const int clampedY = std::max(rcY, std::min(cursorY, rcY + h - 1));

	const double relX = (clampedX - rcX) / (double)w;
	const double relY = (clampedY - rcY) / (double)h;

	const uint32_t sw = obs_source_get_width(src);
	const uint32_t sh = obs_source_get_height(src);
	if (sw == 0 || sh == 0)
		return false;

	sx = (float)(relX * (double)sw);
	sy = (float)(relY * (double)sh);
	return true;
#elif defined(__linux__)
	LinuxRect rc{};
	bool haveRect = false;
	const bool isMonitorCap = (strcmp(id, "monitor_capture") == 0 || strcmp(id, "display_capture") == 0 ||
				   strcmp(id, "screen_capture") == 0 || strcmp(id, "xshm_input") == 0 ||
				   (strstr(id, "monitor") && strstr(id, "capture")));

	if (isMonitorCap) {
		haveRect = match_monitor_rect(src, rc);
	} else if (strcmp(id, "window_capture") == 0 || strcmp(id, "xcomposite_input") == 0 ||
		   strcmp(id, "pipewire-screen-capture-source") == 0 || strcmp(id, "pipewire-window-capture-source") == 0) {
		haveRect = match_window_rect_for_source(src, rc);
	}

	if (!haveRect) {
		if (isMonitorCap)
			return false;
		/* Fallback: use entire virtual screen from XRandR. */
		Display *dpy = XOpenDisplay(nullptr);
		if (dpy) {
			int screen = DefaultScreen(dpy);
			rc.x = 0;
			rc.y = 0;
			rc.w = DisplayWidth(dpy, screen);
			rc.h = DisplayHeight(dpy, screen);
			XCloseDisplay(dpy);
			haveRect = (rc.w > 0 && rc.h > 0);
		}
		if (!haveRect)
			return false;
	}

	if (rc.w <= 0 || rc.h <= 0)
		return false;

	cursorInside = !(cursorX < rc.x || cursorX >= rc.x + rc.w || cursorY < rc.y || cursorY >= rc.y + rc.h);

	const int clampedX = std::max(rc.x, std::min(cursorX, rc.x + rc.w - 1));
	const int clampedY = std::max(rc.y, std::min(cursorY, rc.y + rc.h - 1));

	const double relX = (clampedX - rc.x) / (double)rc.w;
	const double relY = (clampedY - rc.y) / (double)rc.h;

	const uint32_t sw = obs_source_get_width(src);
	const uint32_t sh = obs_source_get_height(src);
	if (sw == 0 || sh == 0)
		return false;

	sx = (float)(relX * (double)sw);
	sy = (float)(relY * (double)sh);
	return true;
#else
	(void)cursorX;
	(void)cursorY;
	(void)sx;
	(void)sy;
	return false;
#endif
}

void ZoominatorController::captureOriginal(obs_sceneitem_t *item)
{
	if (!item)
		return;

	obs_sceneitem_get_pos(item, &orig.pos);
	obs_sceneitem_get_scale(item, &orig.scale);
	orig.rot = obs_sceneitem_get_rot(item);
	orig.align = obs_sceneitem_get_alignment(item);
	orig.boundsType = obs_sceneitem_get_bounds_type(item);
	orig.boundsAlign = obs_sceneitem_get_bounds_alignment(item);
	obs_sceneitem_get_bounds(item, &orig.bounds);
	obs_sceneitem_get_crop(item, &orig.crop);
	orig.valid = true;
	obs_sceneitem_set_alignment(item, OBS_ALIGN_TOP | OBS_ALIGN_LEFT);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	vec2 zb{};
	zb.x = 0.0f; zb.y = 0.0f;
	obs_sceneitem_set_bounds(item, &zb);
}

void ZoominatorController::restoreOriginal(obs_sceneitem_t *item)
{
	if (!item || !orig.valid)
		return;

	obs_sceneitem_set_pos(item, &orig.pos);
	obs_sceneitem_set_scale(item, &orig.scale);
	obs_sceneitem_set_rot(item, orig.rot);
	obs_sceneitem_set_alignment(item, orig.align);
	obs_sceneitem_set_bounds_type(item, orig.boundsType);
	obs_sceneitem_set_bounds_alignment(item, orig.boundsAlign);
	obs_sceneitem_set_bounds(item, &orig.bounds);
	obs_sceneitem_set_crop(item, &orig.crop);
}

void ZoominatorController::rebuildMarkerImage(int opacity255)
{
	const int size = std::clamp(markerSize, 6, 512);
	const QString path = markerImagePath();
	if (path.isEmpty())
		return;

	ensure_parent_dir_exists(path);

	QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);

	QPainter painter(&img);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const int effectiveOpacity = std::clamp(opacity255, 0, 255);
	QColor stroke = QColor::fromRgba(markerColor);
	if (!stroke.isValid())
		stroke = QColor(255, 0, 0, 255);
	if (stroke.alpha() == 0)
		stroke.setAlpha(255);
	stroke.setAlpha((stroke.alpha() * effectiveOpacity) / 255);

	const qreal thickness = std::clamp<qreal>((qreal)markerThickness, 1.0, std::max<qreal>(1.0, size / 2.0 - 2.0));
	const qreal margin = thickness * 0.5 + 1.5;
	const QRectF ringRect(margin, margin, size - margin * 2.0, size - margin * 2.0);

	QPen ringPen(stroke);
	ringPen.setWidthF(thickness);
	ringPen.setCosmetic(true);
	painter.setPen(ringPen);
	painter.setBrush(Qt::NoBrush);
	painter.drawEllipse(ringRect);
	painter.end();

	img.save(path, "PNG");
}

void ZoominatorController::ensureMarkerSource()
{
	rebuildMarkerImage(255);

	if (markerSource)
		return;

	const QString path = markerImagePath();
	if (path.isEmpty())
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path.toUtf8().constData());
	obs_data_set_bool(settings, "unload", false);
	markerSource = obs_source_create_private("image_source", kZoominatorMarkerSourceName, settings);
	obs_data_release(settings);
}

obs_sceneitem_t *ZoominatorController::ensureMarkerItem(obs_scene_t *scene)
{
	if (!scene)
		return nullptr;

	ensureMarkerSource();
	if (!markerSource)
		return nullptr;

	struct Finder {
		obs_source_t *want = nullptr;
		obs_sceneitem_t *found = nullptr;

		static bool enum_cb(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *f = static_cast<Finder *>(param);
			if (!f || f->found)
				return false;
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src == f->want) {
				f->found = item;
				return false;
			}
			return true;
		}
	};

	Finder finder{markerSource, nullptr};
	obs_scene_enum_items(scene, Finder::enum_cb, &finder);
	if (finder.found)
		return finder.found;

	obs_sceneitem_t *item = obs_scene_add(scene, markerSource);
	if (!item)
		return nullptr;

	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_locked(item, true);
	obs_sceneitem_set_visible(item, true);
	return item;
}

void ZoominatorController::hideMarkerInScene(obs_scene_t *scene)
{
	if (!scene || !markerSource)
		return;

	struct Finder {
		obs_source_t *want = nullptr;
		obs_sceneitem_t *found = nullptr;

		static bool enum_cb(obs_scene_t *, obs_sceneitem_t *item, void *param)
		{
			auto *f = static_cast<Finder *>(param);
			if (!f || f->found)
				return false;
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (src == f->want) {
				f->found = item;
				return false;
			}
			return true;
		}
	};

	Finder finder{markerSource, nullptr};
	obs_scene_enum_items(scene, Finder::enum_cb, &finder);
	if (finder.found)
		obs_sceneitem_set_visible(finder.found, false);
}

void ZoominatorController::updateMarkerAppearance(int opacity255)
{
	const int clampedOpacity = std::clamp(opacity255, 0, 255);
	const uint32_t appearanceHash = ((uint32_t)std::clamp(markerSize, 6, 512) << 24) ^ ((uint32_t)std::clamp(markerThickness, 1, 64) << 16) ^ markerColor;
	if (markerSource && markerAppearanceHash == appearanceHash && markerRenderedOpacity == clampedOpacity)
		return;

	rebuildMarkerImage(clampedOpacity);
	ensureMarkerSource();
	if (!markerSource)
		return;

	const QString path = markerImagePath();
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "file", path.toUtf8().constData());
	obs_data_set_bool(settings, "unload", false);
	obs_source_update(markerSource, settings);
	obs_data_release(settings);
	markerAppearanceHash = appearanceHash;
	markerRenderedOpacity = clampedOpacity;
}

bool ZoominatorController::captureMarkerClickPosition()
{
	if (!showCursorMarker || !markerOnlyOnClick)
		return false;

	obs_sceneitem_t *item = findTargetItemInCurrentScene();
	if (!item)
		return false;

	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src)
		return false;

	int cx = 0, cy = 0;
	float mx = 0.f, my = 0.f;
	bool inside = false;
	const bool mapped = getCursorPos(cx, cy) && mapCursorToSourcePixels(src, cx, cy, mx, my, inside);
	if (!mapped || !inside)
		return false;

	markerClickX = mx;
	markerClickY = my;
	markerClickHasPos = true;
	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	static constexpr qint64 kMarkerFadeInMs = 110;
	static constexpr qint64 kMarkerHoldMs = 420;
	static constexpr qint64 kMarkerFadeOutMs = 220;
	markerClickFlashStartMs = nowMs;
	markerClickFlashHoldUntilMs = nowMs + kMarkerFadeInMs + kMarkerHoldMs;
	markerClickFlashFadeOutEndMs = markerClickFlashHoldUntilMs + kMarkerFadeOutMs;
	ensureTicking(true);
	if (debug)
		blog(LOG_INFO, "[Zoominator] Marker anchor captured at %.2f, %.2f", (double)mx, (double)my);
	return true;
}

bool ZoominatorController::isMarkerFlashActive(qint64 nowMs) const
{
	return showCursorMarker && markerOnlyOnClick && markerClickHasPos && markerClickFlashFadeOutEndMs > 0 &&
	       nowMs < markerClickFlashFadeOutEndMs;
}

int ZoominatorController::currentMarkerOpacity(qint64 nowMs)
{
	if (!showCursorMarker || !markerOnlyOnClick || !markerClickHasPos)
		return 0;

	static constexpr qint64 kMarkerFadeInMs = 110;
	if (markerClickFlashFadeOutEndMs <= 0 || nowMs >= markerClickFlashFadeOutEndMs) {
		markerClickHasPos = false;
		markerClickFlashStartMs = 0;
		markerClickFlashHoldUntilMs = 0;
		markerClickFlashFadeOutEndMs = 0;
		return 0;
	}

	if (nowMs < markerClickFlashStartMs + kMarkerFadeInMs) {
		const double tIn = clampd((double)(nowMs - markerClickFlashStartMs) /
					 (double)std::max<qint64>(1, kMarkerFadeInMs),
					 0.0, 1.0);
		return (int)std::round(smoothstep(tIn) * 255.0);
	}

	if (nowMs < markerClickFlashHoldUntilMs)
		return 255;

	const qint64 fadeOutMs = std::max<qint64>(1, markerClickFlashFadeOutEndMs - markerClickFlashHoldUntilMs);
	const double tOut = clampd((double)(nowMs - markerClickFlashHoldUntilMs) / (double)fadeOutMs, 0.0, 1.0);
	return (int)std::round((1.0 - smoothstep(tOut)) * 255.0);
}

void ZoominatorController::updateMarkerPosition(obs_scene_t *scene, double x, double y, int opacity255)
{
	if (!showCursorMarker)
		return;

	const int clampedOpacity = std::clamp(opacity255, 0, 255);
	if (clampedOpacity <= 0) {
		hideMarkerInScene(scene);
		return;
	}

	obs_sceneitem_t *item = ensureMarkerItem(scene);
	if (!item)
		return;

	updateMarkerAppearance(clampedOpacity);
	vec2 pos{};
	pos.x = (float)x;
	pos.y = (float)y;
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_visible(item, true);
}

void ZoominatorController::applyZoom(obs_sceneitem_t *item, obs_source_t *src, double t)
{
	if (!item || !src || !orig.valid)
		return;

	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (sceneSource)
		obs_source_release(sceneSource);

	const double tt = smoothstep(clampd(t, 0.0, 1.0));
	const double zTarget = (zoomFactor <= 1.0) ? 1.0 : zoomFactor;
	const double z = 1.0 + (zTarget - 1.0) * tt;

	const uint32_t sw = obs_source_get_width(src);
	const uint32_t sh = obs_source_get_height(src);
	if (sw == 0 || sh == 0) {
		obs_sceneitem_set_pos(item, &orig.pos);
		return;
	}

	obs_sceneitem_crop crop{};
	obs_sceneitem_get_crop(item, &crop);
	const int32_t visW_i = (int32_t)sw - (int32_t)crop.left - (int32_t)crop.right;
	const int32_t visH_i = (int32_t)sh - (int32_t)crop.top - (int32_t)crop.bottom;
	const double visW = (visW_i > 0) ? (double)visW_i : (double)sw;
	const double visH = (visH_i > 0) ? (double)visH_i : (double)sh;

	float fx = (float)sw * 0.5f;
	float fy = (float)sh * 0.5f;
	float markerSourceX = fx;
	float markerSourceY = fy;
	bool markerHasPoint = false;

	int cx = 0, cy = 0;
	float mx = 0.f, my = 0.f;
	bool inside = false;
	const bool mapped = getCursorPos(cx, cy) && mapCursorToSourcePixels(src, cx, cy, mx, my, inside);

	if (followMouse && followMouseRuntimeEnabled) {
		if (mapped) {
			if (!followHasPos) {
				followX = mx;
				followY = my;
				followHasPos = true;
			} else {
				const double dt = tickTimer.interval() / 1000.0;
				const double alpha = 1.0 - std::exp(-followSpeed * dt);
				followX = (float)(followX + (mx - followX) * alpha);
				followY = (float)(followY + (my - followY) * alpha);
			}
			fx = followX;
			fy = followY;
		} else if (followHasPos) {
			fx = followX;
			fy = followY;
		}
	} else {
		if (!followHasPos) {
			if (mapped) {
				followX = mx;
				followY = my;
				followHasPos = true;
				fx = followX;
				fy = followY;
			}
		} else {
			fx = followX;
			fy = followY;
		}
	}

	if (showCursorMarker) {
		if (markerOnlyOnClick) {
			if (markerClickHasPos) {
				markerSourceX = markerClickX;
				markerSourceY = markerClickY;
				markerHasPoint = true;
			}
		} else if (mapped) {
			markerSourceX = mx;
			markerSourceY = my;
			markerHasPoint = true;
		} else if (followHasPos) {
			markerSourceX = followX;
			markerSourceY = followY;
			markerHasPoint = true;
		}
	}

	obs_video_info ovi{};
	const bool haveVi = obs_get_video_info(&ovi);
	const double cw = haveVi ? (double)ovi.base_width : 1920.0;
	const double ch = haveVi ? (double)ovi.base_height : 1080.0;

	double cover = 1.0;
	
	if (portraitCover) {
		const double sx = cw / visW;
		const double sy = ch / visH;
		cover = (sx > sy) ? sx : sy;
	}

	vec2 sc = orig.scale;
	if (portraitCover) {
		sc.x = (float)std::max((double)sc.x, cover);
		sc.y = (float)std::max((double)sc.y, cover);
	}
	sc.x *= (float)z;
	sc.y *= (float)z;
	obs_sceneitem_set_scale(item, &sc);

	const double centerX = cw * 0.5;
	const double centerY = ch * 0.5;

	const double relFx = clampd((double)fx / (double)sw, 0.0, 1.0);
	const double relFy = clampd((double)fy / (double)sh, 0.0, 1.0);
	fx = (float)((double)crop.left + relFx * visW);
	fy = (float)((double)crop.top + relFy * visH);
	const double fxAdj = (double)fx - (double)crop.left;
	const double fyAdj = (double)fy - (double)crop.top;

	double markerFxAdj = fxAdj;
	double markerFyAdj = fyAdj;
	if (markerHasPoint) {
		const double markerRelFx = clampd((double)markerSourceX / (double)sw, 0.0, 1.0);
		const double markerRelFy = clampd((double)markerSourceY / (double)sh, 0.0, 1.0);
		const double markerFx = (double)crop.left + markerRelFx * visW;
		const double markerFy = (double)crop.top + markerRelFy * visH;
		markerFxAdj = markerFx - (double)crop.left;
		markerFyAdj = markerFy - (double)crop.top;
	}
	double tlx = centerX - fxAdj * (double)sc.x;
	double tly = centerY - fyAdj * (double)sc.y;
	const double itemW = visW * (double)sc.x;
	const double itemH = visH * (double)sc.y;

	if (itemW >= cw) {
		const double minX = cw - itemW;
		tlx = clampd(tlx, minX, 0.0);
	} else {
		tlx = (cw - itemW) * 0.5;
	}
	if (itemH >= ch) {
		const double minY = ch - itemH;
		tly = clampd(tly, minY, 0.0);
	} else {
		tly = (ch - itemH) * 0.5;
	}

	vec2 pos{};
	pos.x = (float)tlx;
	pos.y = (float)tly;
	obs_sceneitem_set_pos(item, &pos);

	if (scene) {
		int markerOpacity = 255;
		if (showCursorMarker && markerHasPoint && markerOnlyOnClick) {
			const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
			markerOpacity = currentMarkerOpacity(nowMs);
		}

		if (showCursorMarker && markerHasPoint && markerOpacity > 0) {
			const double markerX = tlx + markerFxAdj * (double)sc.x;
			const double markerY = tly + markerFyAdj * (double)sc.y;
			updateMarkerPosition(scene, markerX, markerY, markerOpacity);
		} else {
			hideMarkerInScene(scene);
		}
	}
}

void ZoominatorController::onTick()
{
	obs_sceneitem_t *item = findTargetItemInCurrentScene();
	if (!item) {
		if (debug)
			blog(LOG_WARNING, "[Zoominator] No target item (select a capture source).");
		ensureTicking(false);
		resetState();
		return;
	}

	obs_source_t *src = obs_sceneitem_get_source(item);
	if (!src) {
		ensureTicking(false);
		resetState();
		return;
	}

	if (!zoomActive) {
		captureOriginal(item);
		zoomActive = true;
	}

	const int dur = (animDir >= 0) ? animInMs : animOutMs;
	const double dt = tickTimer.interval() / (double)std::max(1, dur);
	animT += (double)animDir * dt;

	if (animT >= 1.0) {
		animT = 1.0;
		animDir = 0;
	}
	if (animT <= 0.0) {
		animT = 0.0;
		animDir = 0;
	}

	const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
	const bool keepMarkerFlashAlive = isMarkerFlashActive(nowMs);

	if (animT == 0.0 && animDir == 0) {
		if (keepMarkerFlashAlive) {
			applyZoom(item, src, 0.0);
			return;
		}

		restoreOriginal(item);
		ensureTicking(false);
		resetState();
		return;
	}

	applyZoom(item, src, animT);
}

static ZoominatorController *g_ctl = nullptr;

static bool mods_current(bool wantCtrl, bool wantAlt, bool wantShift, bool wantWin)
{
#if defined(_WIN32)
	auto down = [](int vk) {
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
	};
	const bool c = down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL);
	const bool a = down(VK_MENU) || down(VK_LMENU) || down(VK_RMENU);
	const bool s = down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT);
	const bool w = down(VK_LWIN) || down(VK_RWIN);
#elif defined(__APPLE__)
	auto down = [](int vk) {
		return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, (CGKeyCode)vk);
	};
	const bool c = down(kVK_Control) || down(kVK_RightControl);
	const bool a = down(kVK_Option) || down(kVK_RightOption);
	const bool s = down(kVK_Shift) || down(kVK_RightShift);
	const bool w = down(kVK_Command) || down(kVK_RightCommand);
#elif defined(__linux__)
	bool c = false, a = false, s = false, w = false;
	Display *dpy = XOpenDisplay(nullptr);
	if (dpy) {
		char keys[32];
		XQueryKeymap(dpy, keys);
		auto isDown = [&](KeySym sym) -> bool {
			KeyCode kc = XKeysymToKeycode(dpy, sym);
			if (kc == 0)
				return false;
			return (keys[kc / 8] & (1 << (kc % 8))) != 0;
		};
		c = isDown(XK_Control_L) || isDown(XK_Control_R);
		a = isDown(XK_Alt_L) || isDown(XK_Alt_R);
		s = isDown(XK_Shift_L) || isDown(XK_Shift_R);
		w = isDown(XK_Super_L) || isDown(XK_Super_R);
		XCloseDisplay(dpy);
	}
#else
	const bool c = false, a = false, s = false, w = false;
#endif

	if (wantCtrl != c)
		return false;
	if (wantAlt != a)
		return false;
	if (wantShift != s)
		return false;
	if (wantWin != w)
		return false;
	return true;
}

#ifdef _WIN32
static inline bool is_modifier_vk(int vk)
{
	switch (vk) {
	case VK_CONTROL:
	case VK_LCONTROL:
	case VK_RCONTROL:
	case VK_MENU:
	case VK_LMENU:
	case VK_RMENU:
	case VK_SHIFT:
	case VK_LSHIFT:
	case VK_RSHIFT:
	case VK_LWIN:
	case VK_RWIN:
		return true;
	default:
		return false;
	}
}

static inline bool is_wanted_modifier_vk(int vk, const ZoominatorController *ctl)
{
	if (!ctl)
		return false;
	if (ctl->modCtrl && (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL))
		return true;
	if (ctl->modAlt && (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU))
		return true;
	if (ctl->modShift && (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT))
		return true;
	if (ctl->modWin && (vk == VK_LWIN || vk == VK_RWIN))
		return true;
	return false;
}

static bool vk_matches(int pressedVk, int wantVk)
{
	if (pressedVk == wantVk)
		return true;

	if (wantVk >= '0' && wantVk <= '9') {
		const int d = wantVk - '0';
		return pressedVk == (VK_NUMPAD0 + d);
	}
	if (wantVk >= VK_NUMPAD0 && wantVk <= VK_NUMPAD9) {
		const int d = wantVk - VK_NUMPAD0;
		return pressedVk == ('0' + d);
	}

	return false;
}

LRESULT CALLBACK ZoominatorController::kb_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && g_ctl) {
		const auto *k = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
		const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
		const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

		if (!k || !(down || up))
			return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);

		const int vk = (int)k->vkCode;

		if (g_ctl->followToggleHkValid && down && g_ctl->followToggleHotkeyVk != 0 &&
		    vk_matches(vk, g_ctl->followToggleHotkeyVk) &&
		    mods_current(g_ctl->followToggleModCtrl, g_ctl->followToggleModAlt, g_ctl->followToggleModShift,
				 g_ctl->followToggleModWin)) {
			g_ctl->toggleFollowMouseRuntime();
		}

		if (!(g_ctl->hkValid && g_ctl->triggerType == "keyboard"))
			return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);

		if (g_ctl->hotkeyVk == 0) {
			if (!is_modifier_vk(vk))
				return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);

			if (!is_wanted_modifier_vk(vk, g_ctl))
				return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);

			const bool matchNow = mods_current(g_ctl->modCtrl, g_ctl->modAlt, g_ctl->modShift, g_ctl->modWin);

			if (g_ctl->hotkeyMode == "toggle") {
				if (down && matchNow)
					g_ctl->onTriggerDown();
			} else {
				if (down && matchNow)
					g_ctl->onTriggerDown();
				if (up && g_ctl->zoomPressed && !matchNow)
					g_ctl->onTriggerUp();
			}

			return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);
		}

		if (vk_matches(vk, g_ctl->hotkeyVk)) {
			if (mods_current(g_ctl->modCtrl, g_ctl->modAlt, g_ctl->modShift, g_ctl->modWin)) {
				if (g_ctl->hotkeyMode == "toggle") {
					if (down)
						g_ctl->onTriggerDown();
				} else {
					if (down)
						g_ctl->onTriggerDown();
					if (up)
						g_ctl->onTriggerUp();
				}
			}
		}
	}
	return CallNextHookEx((HHOOK)g_ctl->keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK ZoominatorController::mouse_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && g_ctl) {
		const auto *m = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
		if (!m)
			return CallNextHookEx((HHOOK)g_ctl->mouseHook, nCode, wParam, lParam);

		const bool down = (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN ||
				   wParam == WM_XBUTTONDOWN);
		const bool up = (wParam == WM_LBUTTONUP || wParam == WM_RBUTTONUP || wParam == WM_MBUTTONUP ||
				 wParam == WM_XBUTTONUP);

		if (down && g_ctl->markerOnlyOnClick)
			g_ctl->captureMarkerClickPosition();

		if (g_ctl->triggerType == "mouse" && (down || up)) {
			unsigned short mouseData = (unsigned short)HIWORD(m->mouseData);
			if (mods_current(g_ctl->modCtrl, g_ctl->modAlt, g_ctl->modShift, g_ctl->modWin)) {
				if (g_ctl->triggerMatchesMouse((unsigned int)wParam, mouseData)) {
					if (g_ctl->hotkeyMode == "toggle") {
						if (down)
							g_ctl->onTriggerDown();
					} else {
						if (down)
							g_ctl->onTriggerDown();
						if (up)
							g_ctl->onTriggerUp();
					}
				}
			}
		}
	}
	return CallNextHookEx((HHOOK)g_ctl->mouseHook, nCode, wParam, lParam);
}
#endif // _WIN32

#ifdef __APPLE__
static inline bool is_modifier_vk(int vk)
{
	return vk == kVK_Control || vk == kVK_RightControl || vk == kVK_Option || vk == kVK_RightOption ||
	       vk == kVK_Shift || vk == kVK_RightShift || vk == kVK_Command || vk == kVK_RightCommand;
}

static inline bool is_wanted_modifier_vk(int vk, const ZoominatorController *ctl)
{
	if (!ctl)
		return false;
	if (ctl->modCtrl && (vk == kVK_Control || vk == kVK_RightControl))
		return true;
	if (ctl->modAlt && (vk == kVK_Option || vk == kVK_RightOption))
		return true;
	if (ctl->modShift && (vk == kVK_Shift || vk == kVK_RightShift))
		return true;
	if (ctl->modWin && (vk == kVK_Command || vk == kVK_RightCommand))
		return true;
	return false;
}

static bool vk_matches(int pressedVk, int wantVk)
{
	if (pressedVk == wantVk)
		return true;

	if (wantVk >= kVK_ANSI_Keypad0 && wantVk <= kVK_ANSI_Keypad9) {
		static const int numRow[] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
					     kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
		return pressedVk == numRow[wantVk - kVK_ANSI_Keypad0];
	}
	return false;
}

static bool mac_mouse_button_matches(CGEventType type, int64_t buttonNumber, const QString &want)
{
	const bool isDown = (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown || type == kCGEventOtherMouseDown);
	const bool isUp = (type == kCGEventLeftMouseUp || type == kCGEventRightMouseUp || type == kCGEventOtherMouseUp);
	if (!isDown && !isUp)
		return false;

	if (want == "left")
		return buttonNumber == 0;
	if (want == "right")
		return buttonNumber == 1;
	if (want == "middle")
		return buttonNumber == 2;
	if (want == "x1")
		return buttonNumber == 3;
	if (want == "x2")
		return buttonNumber == 4;
	return false;
}

CGEventRef ZoominatorController::eventTapCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event,
						  void *refcon)
{
	(void)proxy;
	auto *ctl = static_cast<ZoominatorController *>(refcon);
	if (!ctl)
		return event;

	if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
		if (ctl->eventTap)
			CGEventTapEnable(ctl->eventTap, true);
		return event;
	}

	if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
		const int keycode = (int)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
		const bool down = (type == kCGEventKeyDown);

		if (ctl->followToggleHkValid && down && ctl->followToggleHotkeyVk != 0 &&
		    vk_matches(keycode, ctl->followToggleHotkeyVk) &&
		    mods_current(ctl->followToggleModCtrl, ctl->followToggleModAlt, ctl->followToggleModShift,
				 ctl->followToggleModWin)) {
			ctl->toggleFollowMouseRuntime();
		}
	}

	if (ctl->triggerType == "keyboard" && ctl->hkValid) {
		if (type == kCGEventKeyDown || type == kCGEventKeyUp) {
			const int keycode = (int)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
			const bool down = (type == kCGEventKeyDown);
			const bool up = (type == kCGEventKeyUp);

			if (ctl->hotkeyVk == 0) {
				return event;
			}

			if (vk_matches(keycode, ctl->hotkeyVk)) {
				if (mods_current(ctl->modCtrl, ctl->modAlt, ctl->modShift, ctl->modWin)) {
					if (ctl->hotkeyMode == "toggle") {
						if (down)
							ctl->onTriggerDown();
					} else {
						if (down)
							ctl->onTriggerDown();
						if (up)
							ctl->onTriggerUp();
					}
				}
			}
			return event;
		}

		if (type == kCGEventFlagsChanged) {
			if (ctl->hotkeyVk == 0) {
				const bool matchNow =
					mods_current(ctl->modCtrl, ctl->modAlt, ctl->modShift, ctl->modWin);
				if (ctl->hotkeyMode == "toggle") {
					if (matchNow && !ctl->zoomPressed && !ctl->zoomLatched)
						ctl->onTriggerDown();
					else if (matchNow && ctl->zoomLatched)
						ctl->onTriggerDown();
				} else {
					if (matchNow && !ctl->zoomPressed)
						ctl->onTriggerDown();
					if (!matchNow && ctl->zoomPressed)
						ctl->onTriggerUp();
				}
			}
			return event;
		}
	}

	const bool isMouseDown = (type == kCGEventLeftMouseDown || type == kCGEventRightMouseDown ||
				type == kCGEventOtherMouseDown);
	const bool isMouseUp = (type == kCGEventLeftMouseUp || type == kCGEventRightMouseUp ||
			      type == kCGEventOtherMouseUp);
	if (isMouseDown && ctl->markerOnlyOnClick)
		ctl->captureMarkerClickPosition();

	if (ctl->triggerType == "mouse") {
		if (isMouseDown || isMouseUp) {
			const int64_t btn = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
			if (mods_current(ctl->modCtrl, ctl->modAlt, ctl->modShift, ctl->modWin)) {
				if (mac_mouse_button_matches(type, btn, ctl->mouseButton)) {
					if (ctl->hotkeyMode == "toggle") {
						if (isMouseDown)
							ctl->onTriggerDown();
					} else {
						if (isMouseDown)
							ctl->onTriggerDown();
						if (isMouseUp)
							ctl->onTriggerUp();
					}
				}
			}
		}
	}

	return event;
}
#endif // __APPLE__

#ifdef __linux__
static inline bool is_modifier_vk(int vk)
{
	return vk == XK_Control_L || vk == XK_Control_R || vk == XK_Alt_L || vk == XK_Alt_R || vk == XK_Shift_L ||
	       vk == XK_Shift_R || vk == XK_Super_L || vk == XK_Super_R;
}

static inline bool is_wanted_modifier_vk(int vk, const ZoominatorController *ctl)
{
	if (!ctl)
		return false;
	if (ctl->modCtrl && (vk == XK_Control_L || vk == XK_Control_R))
		return true;
	if (ctl->modAlt && (vk == XK_Alt_L || vk == XK_Alt_R))
		return true;
	if (ctl->modShift && (vk == XK_Shift_L || vk == XK_Shift_R))
		return true;
	if (ctl->modWin && (vk == XK_Super_L || vk == XK_Super_R))
		return true;
	return false;
}

static bool vk_matches(int pressedVk, int wantVk)
{
	if (pressedVk == wantVk)
		return true;

	/* Match lowercase letters to their keysym too. */
	if (wantVk >= XK_a && wantVk <= XK_z)
		return pressedVk == (wantVk - XK_a + XK_A);
	if (wantVk >= XK_A && wantVk <= XK_Z)
		return pressedVk == (wantVk - XK_A + XK_a);

	/* Match numpad to number row. */
	if (wantVk >= XK_KP_0 && wantVk <= XK_KP_9)
		return pressedVk == (XK_0 + (wantVk - XK_KP_0));
	if (wantVk >= XK_0 && wantVk <= XK_9)
		return pressedVk == (XK_KP_0 + (wantVk - XK_0));

	return false;
}

static bool linux_button_matches(int button, const QString &want)
{
	if (want == "left")
		return button == 1;
	if (want == "middle")
		return button == 2;
	if (want == "right")
		return button == 3;
	if (want == "x1")
		return button == 8;
	if (want == "x2")
		return button == 9;
	return false;
}

void ZoominatorController::processXInput2Events()
{
	if (!xiDisplay)
		return;

	while (XPending(xiDisplay)) {
		XEvent ev;
		XNextEvent(xiDisplay, &ev);

		if (ev.xcookie.type != GenericEvent || ev.xcookie.extension != xiOpcode)
			continue;
		if (!XGetEventData(xiDisplay, &ev.xcookie))
			continue;

		const int evtype = ev.xcookie.evtype;

		if (evtype == XI_RawKeyPress || evtype == XI_RawKeyRelease) {
			XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
			const int keycode = raw->detail;
			KeySym sym = XkbKeycodeToKeysym(xiDisplay, keycode, 0, 0);
			const bool down = (evtype == XI_RawKeyPress);

			/* Follow-toggle hotkey. */
			if (followToggleHkValid && down && followToggleHotkeyVk != 0 &&
			    vk_matches((int)sym, followToggleHotkeyVk) &&
			    mods_current(followToggleModCtrl, followToggleModAlt, followToggleModShift,
					 followToggleModWin)) {
				toggleFollowMouseRuntime();
			}

			/* Main keyboard trigger. */
			if (hkValid && triggerType == "keyboard") {
				if (hotkeyVk == 0) {
					/* Modifier-only trigger. */
					if (is_modifier_vk((int)sym) && is_wanted_modifier_vk((int)sym, this)) {
						const bool matchNow = mods_current(modCtrl, modAlt, modShift, modWin);
						if (hotkeyMode == "toggle") {
							if (down && matchNow)
								onTriggerDown();
						} else {
							if (down && matchNow)
								onTriggerDown();
							if (!down && zoomPressed && !matchNow)
								onTriggerUp();
						}
					}
				} else if (vk_matches((int)sym, hotkeyVk)) {
					if (mods_current(modCtrl, modAlt, modShift, modWin)) {
						if (hotkeyMode == "toggle") {
							if (down)
								onTriggerDown();
						} else {
							if (down)
								onTriggerDown();
							if (!down)
								onTriggerUp();
						}
					}
				}
			}
		} else if (evtype == XI_RawButtonPress || evtype == XI_RawButtonRelease) {
			XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
			const int button = raw->detail;
			const bool down = (evtype == XI_RawButtonPress);
			const bool up = (evtype == XI_RawButtonRelease);

			/* Scroll wheel events are buttons 4-7 on X11, ignore them. */
			if (button >= 4 && button <= 7) {
				XFreeEventData(xiDisplay, &ev.xcookie);
				continue;
			}

			if (down && markerOnlyOnClick)
				captureMarkerClickPosition();

			if (triggerType == "mouse" && (down || up)) {
				if (mods_current(modCtrl, modAlt, modShift, modWin)) {
					if (linux_button_matches(button, mouseButton)) {
						if (hotkeyMode == "toggle") {
							if (down)
								onTriggerDown();
						} else {
							if (down)
								onTriggerDown();
							if (up)
								onTriggerUp();
						}
					}
				}
			}
		}

		XFreeEventData(xiDisplay, &ev.xcookie);
	}
}
#endif // __linux__

bool ZoominatorController::modsMatch() const
{
	return mods_current(modCtrl, modAlt, modShift, modWin);
}

bool ZoominatorController::triggerMatchesMouse(unsigned int msg, unsigned short mouseData) const
{
#ifdef _WIN32
	const QString b = mouseButton;
	if (b == "left")
		return msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP;
	if (b == "right")
		return msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP;
	if (b == "middle")
		return msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP;
	if (b == "x1")
		return (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) && mouseData == XBUTTON1;
	if (b == "x2")
		return (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) && mouseData == XBUTTON2;
	return false;
#elif defined(__linux__)
	/* On Linux, msg holds the X11 button number, mouseData is unused. */
	(void)mouseData;
	return linux_button_matches((int)msg, mouseButton);
#else
	(void)msg;
	(void)mouseData;
	return false;
#endif
}

void ZoominatorController::onTriggerDown()
{
	if (debug)
		blog(LOG_INFO, "[Zoominator] Trigger DOWN");

	if (hotkeyMode == "toggle") {
		zoomLatched = !zoomLatched;
		if (zoomLatched) {
			followHasPos = false;
			startZoomIn();
		} else {
			startZoomOut();
		}
		return;
	}

	zoomPressed = true;
	followHasPos = false;
	startZoomIn();
}

void ZoominatorController::onTriggerUp()
{
	if (debug)
		blog(LOG_INFO, "[Zoominator] Trigger UP");
	zoomPressed = false;
	startZoomOut();
}

void ZoominatorController::toggleFollowMouseRuntime()
{
	if (!followMouse)
		return;

	followMouseRuntimeEnabled = !followMouseRuntimeEnabled;
	if (!followMouseRuntimeEnabled)
		followHasPos = true;

	if (debug) {
		blog(LOG_INFO, "[Zoominator] Follow mouse %s", followMouseRuntimeEnabled ? "ENABLED" : "DISABLED");
	}
}

static int qtKeyToVk(int qtKey)
{
#if defined(__APPLE__)
	if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
		static const int map[] = {kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F,
					  kVK_ANSI_G, kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L,
					  kVK_ANSI_M, kVK_ANSI_N, kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R,
					  kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U, kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X,
					  kVK_ANSI_Y, kVK_ANSI_Z};
		return map[qtKey - Qt::Key_A];
	}
	if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
		static const int map[] = {kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
					  kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
		return map[qtKey - Qt::Key_0];
	}
	if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F20) {
		static const int map[] = {kVK_F1,  kVK_F2,  kVK_F3,  kVK_F4,  kVK_F5,  kVK_F6,  kVK_F7,
					  kVK_F8,  kVK_F9,  kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14,
					  kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20};
		return map[qtKey - Qt::Key_F1];
	}
	switch (qtKey) {
	case Qt::Key_Space:
		return kVK_Space;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		return kVK_Return;
	case Qt::Key_Escape:
		return kVK_Escape;
	case Qt::Key_Tab:
		return kVK_Tab;
	case Qt::Key_Backspace:
		return kVK_Delete; 
	case Qt::Key_Delete:
		return kVK_ForwardDelete;
	case Qt::Key_Left:
		return kVK_LeftArrow;
	case Qt::Key_Right:
		return kVK_RightArrow;
	case Qt::Key_Up:
		return kVK_UpArrow;
	case Qt::Key_Down:
		return kVK_DownArrow;
	case Qt::Key_Home:
		return kVK_Home;
	case Qt::Key_End:
		return kVK_End;
	case Qt::Key_PageUp:
		return kVK_PageUp;
	case Qt::Key_PageDown:
		return kVK_PageDown;
	case Qt::Key_CapsLock:
		return kVK_CapsLock;
	case Qt::Key_Shift:
		return kVK_Shift;
	case Qt::Key_Control:
		return kVK_Control;
	case Qt::Key_Alt:
		return kVK_Option;
	case Qt::Key_Meta:
		return kVK_Command;
	case Qt::Key_Semicolon:
		return kVK_ANSI_Semicolon;
	case Qt::Key_Equal:
		return kVK_ANSI_Equal;
	case Qt::Key_Comma:
		return kVK_ANSI_Comma;
	case Qt::Key_Minus:
		return kVK_ANSI_Minus;
	case Qt::Key_Period:
		return kVK_ANSI_Period;
	case Qt::Key_Slash:
		return kVK_ANSI_Slash;
	case Qt::Key_QuoteLeft:
		return kVK_ANSI_Grave;
	case Qt::Key_BracketLeft:
		return kVK_ANSI_LeftBracket;
	case Qt::Key_Backslash:
		return kVK_ANSI_Backslash;
	case Qt::Key_BracketRight:
		return kVK_ANSI_RightBracket;
	default:
		break;
	}
	return 0;
#elif defined(__linux__)
	/* On Linux we use X11 keysyms (XK_*) as our "virtual keycode" space. */
	if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
		return XK_a + (qtKey - Qt::Key_A);
	if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
		return XK_0 + (qtKey - Qt::Key_0);
	if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
		return XK_F1 + (qtKey - Qt::Key_F1);

	switch (qtKey) {
	case Qt::Key_Space:
		return XK_space;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		return XK_Return;
	case Qt::Key_Escape:
		return XK_Escape;
	case Qt::Key_Tab:
		return XK_Tab;
	case Qt::Key_Backspace:
		return XK_BackSpace;
	case Qt::Key_Delete:
		return XK_Delete;
	case Qt::Key_Left:
		return XK_Left;
	case Qt::Key_Right:
		return XK_Right;
	case Qt::Key_Up:
		return XK_Up;
	case Qt::Key_Down:
		return XK_Down;
	case Qt::Key_Home:
		return XK_Home;
	case Qt::Key_End:
		return XK_End;
	case Qt::Key_PageUp:
		return XK_Page_Up;
	case Qt::Key_PageDown:
		return XK_Page_Down;
	case Qt::Key_Insert:
		return XK_Insert;
	case Qt::Key_Print:
		return XK_Print;
	case Qt::Key_Pause:
		return XK_Pause;
	case Qt::Key_CapsLock:
		return XK_Caps_Lock;
	case Qt::Key_Shift:
		return XK_Shift_L;
	case Qt::Key_Control:
		return XK_Control_L;
	case Qt::Key_Alt:
		return XK_Alt_L;
	case Qt::Key_Meta:
		return XK_Super_L;
	case Qt::Key_Semicolon:
		return XK_semicolon;
	case Qt::Key_Equal:
		return XK_equal;
	case Qt::Key_Comma:
		return XK_comma;
	case Qt::Key_Minus:
		return XK_minus;
	case Qt::Key_Period:
		return XK_period;
	case Qt::Key_Slash:
		return XK_slash;
	case Qt::Key_QuoteLeft:
		return XK_grave;
	case Qt::Key_BracketLeft:
		return XK_bracketleft;
	case Qt::Key_Backslash:
		return XK_backslash;
	case Qt::Key_BracketRight:
		return XK_bracketright;
	default:
		break;
	}

	/* Fallback: many Qt keys match the corresponding X11 keysym for printable ASCII. */
	if (qtKey >= 0x20 && qtKey <= 0x7E)
		return qtKey;

	return 0;
#else
	if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
		return 'A' + (qtKey - Qt::Key_A);
	if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
		return '0' + (qtKey - Qt::Key_0);
	if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
		return VK_F1 + (qtKey - Qt::Key_F1);

	switch (qtKey) {
	case Qt::Key_Space:
		return VK_SPACE;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		return VK_RETURN;
	case Qt::Key_Escape:
		return VK_ESCAPE;
	case Qt::Key_Tab:
		return VK_TAB;
	case Qt::Key_Backspace:
		return VK_BACK;
	case Qt::Key_Left:
		return VK_LEFT;
	case Qt::Key_Right:
		return VK_RIGHT;
	case Qt::Key_Up:
		return VK_UP;
	case Qt::Key_Down:
		return VK_DOWN;
	case Qt::Key_Insert:
		return VK_INSERT;
	case Qt::Key_Delete:
		return VK_DELETE;
	case Qt::Key_Home:
		return VK_HOME;
	case Qt::Key_End:
		return VK_END;
	case Qt::Key_PageUp:
		return VK_PRIOR;
	case Qt::Key_PageDown:
		return VK_NEXT;
	case Qt::Key_Print:
		return VK_SNAPSHOT;
	case Qt::Key_Pause:
		return VK_PAUSE;
	case Qt::Key_CapsLock:
		return VK_CAPITAL;
	case Qt::Key_Clear:
		return VK_CLEAR;
	case Qt::Key_Shift:
		return VK_SHIFT;
	case Qt::Key_Control:
		return VK_CONTROL;
	case Qt::Key_Alt:
		return VK_MENU;
	case Qt::Key_Meta:
		return VK_LWIN; 
	case Qt::Key_Semicolon:
		return VK_OEM_1;
	case Qt::Key_Plus:
		return VK_OEM_PLUS;
	case Qt::Key_Comma:
		return VK_OEM_COMMA;
	case Qt::Key_Minus:
		return VK_OEM_MINUS;
	case Qt::Key_Period:
		return VK_OEM_PERIOD;
	case Qt::Key_Slash:
		return VK_OEM_2;
	case Qt::Key_QuoteLeft:
		return VK_OEM_3; 
	case Qt::Key_BracketLeft:
		return VK_OEM_4;
	case Qt::Key_Backslash:
		return VK_OEM_5;
	case Qt::Key_BracketRight:
		return VK_OEM_6;

	default:
		break;
	}

	if (qtKey >= 0x20 && qtKey <= 0x7E)
		return qtKey;

	return 0;
#endif
}

void ZoominatorController::rebuildTriggersFromSettings()
{
	hkValid = false;
	hotkeyVk = 0;
	followToggleHkValid = false;
	followToggleHotkeyVk = 0;
	followToggleModCtrl = false;
	followToggleModAlt = false;
	followToggleModShift = false;
	followToggleModWin = false;

	QKeySequence followSeq(followToggleHotkeySequence);
	if (!followSeq.isEmpty()) {
		const QKeyCombination kc = followSeq[0];
		const auto mods = kc.keyboardModifiers();
		const int key = int(kc.key());
		followToggleHotkeyVk = qtKeyToVk(key);
		followToggleModCtrl = mods.testFlag(Qt::ControlModifier);
		followToggleModAlt = mods.testFlag(Qt::AltModifier);
		followToggleModShift = mods.testFlag(Qt::ShiftModifier);
		followToggleModWin = mods.testFlag(Qt::MetaModifier);

#ifdef _WIN32
		const bool keyIsModifier = (followToggleHotkeyVk == VK_CONTROL || followToggleHotkeyVk == VK_LCONTROL ||
					    followToggleHotkeyVk == VK_RCONTROL || followToggleHotkeyVk == VK_MENU ||
					    followToggleHotkeyVk == VK_LMENU || followToggleHotkeyVk == VK_RMENU ||
					    followToggleHotkeyVk == VK_SHIFT || followToggleHotkeyVk == VK_LSHIFT ||
					    followToggleHotkeyVk == VK_RSHIFT || followToggleHotkeyVk == VK_LWIN ||
					    followToggleHotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
		const bool keyIsModifier =
			(followToggleHotkeyVk == kVK_Control || followToggleHotkeyVk == kVK_RightControl ||
			 followToggleHotkeyVk == kVK_Option || followToggleHotkeyVk == kVK_RightOption ||
			 followToggleHotkeyVk == kVK_Shift || followToggleHotkeyVk == kVK_RightShift ||
			 followToggleHotkeyVk == kVK_Command || followToggleHotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
		const bool keyIsModifier =
			(followToggleHotkeyVk == XK_Control_L || followToggleHotkeyVk == XK_Control_R ||
			 followToggleHotkeyVk == XK_Alt_L || followToggleHotkeyVk == XK_Alt_R ||
			 followToggleHotkeyVk == XK_Shift_L || followToggleHotkeyVk == XK_Shift_R ||
			 followToggleHotkeyVk == XK_Super_L || followToggleHotkeyVk == XK_Super_R);
#else
		const bool keyIsModifier = false;
#endif

		if (keyIsModifier && mods == Qt::NoModifier) {
#ifdef _WIN32
			followToggleModCtrl = (followToggleHotkeyVk == VK_CONTROL ||
					       followToggleHotkeyVk == VK_LCONTROL ||
					       followToggleHotkeyVk == VK_RCONTROL);
			followToggleModAlt = (followToggleHotkeyVk == VK_MENU || followToggleHotkeyVk == VK_LMENU ||
					      followToggleHotkeyVk == VK_RMENU);
			followToggleModShift = (followToggleHotkeyVk == VK_SHIFT || followToggleHotkeyVk == VK_LSHIFT ||
						followToggleHotkeyVk == VK_RSHIFT);
			followToggleModWin = (followToggleHotkeyVk == VK_LWIN || followToggleHotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
			followToggleModCtrl =
				(followToggleHotkeyVk == kVK_Control || followToggleHotkeyVk == kVK_RightControl);
			followToggleModAlt =
				(followToggleHotkeyVk == kVK_Option || followToggleHotkeyVk == kVK_RightOption);
			followToggleModShift =
				(followToggleHotkeyVk == kVK_Shift || followToggleHotkeyVk == kVK_RightShift);
			followToggleModWin =
				(followToggleHotkeyVk == kVK_Command || followToggleHotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
			followToggleModCtrl =
				(followToggleHotkeyVk == XK_Control_L || followToggleHotkeyVk == XK_Control_R);
			followToggleModAlt =
				(followToggleHotkeyVk == XK_Alt_L || followToggleHotkeyVk == XK_Alt_R);
			followToggleModShift =
				(followToggleHotkeyVk == XK_Shift_L || followToggleHotkeyVk == XK_Shift_R);
			followToggleModWin =
				(followToggleHotkeyVk == XK_Super_L || followToggleHotkeyVk == XK_Super_R);
#else
			followToggleHotkeyVk = 0;
#endif
		}

		followToggleHkValid = (followToggleHotkeyVk != 0) &&
				      (followToggleModCtrl || followToggleModAlt || followToggleModShift ||
				       followToggleModWin || key != 0);
	}

	if (triggerType != "mouse")
		triggerType = "keyboard";

	if (triggerType == "keyboard") {
		QKeySequence seq(hotkeySequence);
		if (seq.isEmpty()) {
			hotkeyVk = 0;
			hkValid = (modCtrl || modAlt || modShift || modWin);
			if (debug)
				blog(LOG_INFO,
				     "[Zoominator] Hotkey empty; using modifier-only trigger (ctrl=%d alt=%d shift=%d win=%d valid=%d)",
				     modCtrl ? 1 : 0, modAlt ? 1 : 0, modShift ? 1 : 0, modWin ? 1 : 0, hkValid ? 1 : 0);
		} else {
			const QKeyCombination kc = seq[0];
			const auto mods = kc.keyboardModifiers();
			const int key = int(kc.key());
			hotkeyVk = qtKeyToVk(key);

			const bool noMods = (mods == Qt::NoModifier);
#ifdef _WIN32
			const bool keyIsModifier = (hotkeyVk == VK_CONTROL || hotkeyVk == VK_LCONTROL || hotkeyVk == VK_RCONTROL ||
						    hotkeyVk == VK_MENU || hotkeyVk == VK_LMENU || hotkeyVk == VK_RMENU ||
						    hotkeyVk == VK_SHIFT || hotkeyVk == VK_LSHIFT || hotkeyVk == VK_RSHIFT ||
						    hotkeyVk == VK_LWIN || hotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
			const bool keyIsModifier = (hotkeyVk == kVK_Control || hotkeyVk == kVK_RightControl ||
						    hotkeyVk == kVK_Option || hotkeyVk == kVK_RightOption ||
						    hotkeyVk == kVK_Shift || hotkeyVk == kVK_RightShift ||
						    hotkeyVk == kVK_Command || hotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
			const bool keyIsModifier = (hotkeyVk == XK_Control_L || hotkeyVk == XK_Control_R ||
						    hotkeyVk == XK_Alt_L || hotkeyVk == XK_Alt_R ||
						    hotkeyVk == XK_Shift_L || hotkeyVk == XK_Shift_R ||
						    hotkeyVk == XK_Super_L || hotkeyVk == XK_Super_R);
#else
			const bool keyIsModifier = (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta);
#endif
			if (noMods && keyIsModifier) {
#ifdef _WIN32
				modCtrl = (hotkeyVk == VK_CONTROL || hotkeyVk == VK_LCONTROL || hotkeyVk == VK_RCONTROL);
				modAlt = (hotkeyVk == VK_MENU || hotkeyVk == VK_LMENU || hotkeyVk == VK_RMENU);
				modShift = (hotkeyVk == VK_SHIFT || hotkeyVk == VK_LSHIFT || hotkeyVk == VK_RSHIFT);
				modWin = (hotkeyVk == VK_LWIN || hotkeyVk == VK_RWIN);
#elif defined(__APPLE__)
				modCtrl = (hotkeyVk == kVK_Control || hotkeyVk == kVK_RightControl);
				modAlt = (hotkeyVk == kVK_Option || hotkeyVk == kVK_RightOption);
				modShift = (hotkeyVk == kVK_Shift || hotkeyVk == kVK_RightShift);
				modWin = (hotkeyVk == kVK_Command || hotkeyVk == kVK_RightCommand);
#elif defined(__linux__)
				modCtrl = (hotkeyVk == XK_Control_L || hotkeyVk == XK_Control_R);
				modAlt = (hotkeyVk == XK_Alt_L || hotkeyVk == XK_Alt_R);
				modShift = (hotkeyVk == XK_Shift_L || hotkeyVk == XK_Shift_R);
				modWin = (hotkeyVk == XK_Super_L || hotkeyVk == XK_Super_R);
#else
				modCtrl = (key == Qt::Key_Control);
				modAlt = (key == Qt::Key_Alt);
				modShift = (key == Qt::Key_Shift);
				modWin = (key == Qt::Key_Meta);
#endif
				hotkeyVk = 0;
				hkValid = (modCtrl || modAlt || modShift || modWin);
				if (debug)
					blog(LOG_INFO,
					     "[Zoominator] Single-modifier hotkey parsed; using modifier-only trigger (ctrl=%d alt=%d shift=%d win=%d valid=%d)",
					     modCtrl ? 1 : 0, modAlt ? 1 : 0, modShift ? 1 : 0, modWin ? 1 : 0, hkValid ? 1 : 0);
			} else {
				modCtrl = mods.testFlag(Qt::ControlModifier);
				modAlt = mods.testFlag(Qt::AltModifier);
				modShift = mods.testFlag(Qt::ShiftModifier);
				modWin = mods.testFlag(Qt::MetaModifier);

				hkValid = (hotkeyVk != 0);
				if (debug)
					blog(LOG_INFO,
					     "[Zoominator] Hotkey parsed: '%s' vk=%d ctrl=%d alt=%d shift=%d win=%d valid=%d",
					     hotkeySequence.toUtf8().constData(), hotkeyVk, modCtrl ? 1 : 0, modAlt ? 1 : 0,
					     modShift ? 1 : 0, modWin ? 1 : 0, hkValid ? 1 : 0);
			}
		}
	} else {
		hkValid = true;
	}
}

void ZoominatorController::installHooks()
{
#ifdef _WIN32
	g_ctl = this;

	if (!keyboardHook) {
		keyboardHook = (void *)SetWindowsHookExW(WH_KEYBOARD_LL, kb_hook_proc, GetModuleHandleW(nullptr), 0);
	}
	if (!mouseHook) {
		mouseHook = (void *)SetWindowsHookExW(WH_MOUSE_LL, mouse_hook_proc, GetModuleHandleW(nullptr), 0);
	}
#elif defined(__APPLE__)
	g_ctl = this;

	if (!eventTap) {
		CGEventMask mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
				   CGEventMaskBit(kCGEventFlagsChanged) | CGEventMaskBit(kCGEventLeftMouseDown) |
				   CGEventMaskBit(kCGEventLeftMouseUp) | CGEventMaskBit(kCGEventRightMouseDown) |
				   CGEventMaskBit(kCGEventRightMouseUp) | CGEventMaskBit(kCGEventOtherMouseDown) |
				   CGEventMaskBit(kCGEventOtherMouseUp);
		eventTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask,
					    eventTapCallback, this);
		if (eventTap) {
			runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
			CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopCommonModes);
			CGEventTapEnable(eventTap, true);
		} else {
			blog(LOG_WARNING,
			     "[Zoominator] Failed to create CGEventTap. "
			     "Grant Accessibility permission to OBS in System Settings > Privacy & Security > Accessibility.");
		}
	}
#elif defined(__linux__)
	g_ctl = this;

	if (!xiDisplay) {
		xiDisplay = XOpenDisplay(nullptr);
		if (!xiDisplay) {
			blog(LOG_WARNING, "[Zoominator] Failed to open X11 display for input hooks.");
			return;
		}

		/* Check for XInput2 extension. */
		int event, error;
		if (!XQueryExtension(xiDisplay, "XInputExtension", &xiOpcode, &event, &error)) {
			blog(LOG_WARNING, "[Zoominator] XInput2 extension not available.");
			XCloseDisplay(xiDisplay);
			xiDisplay = nullptr;
			return;
		}

		/* Request XInput 2.0. */
		int major = 2, minor = 0;
		if (XIQueryVersion(xiDisplay, &major, &minor) != Success) {
			blog(LOG_WARNING, "[Zoominator] XInput2 version query failed.");
			XCloseDisplay(xiDisplay);
			xiDisplay = nullptr;
			return;
		}

		/* Select raw key and button events on the root window. */
		unsigned char mask_bits[(XI_LASTEVENT + 7) / 8] = {};
		XIEventMask evmask;
		evmask.deviceid = XIAllMasterDevices;
		evmask.mask_len = sizeof(mask_bits);
		evmask.mask = mask_bits;
		XISetMask(mask_bits, XI_RawKeyPress);
		XISetMask(mask_bits, XI_RawKeyRelease);
		XISetMask(mask_bits, XI_RawButtonPress);
		XISetMask(mask_bits, XI_RawButtonRelease);
		XISelectEvents(xiDisplay, DefaultRootWindow(xiDisplay), &evmask, 1);
		XFlush(xiDisplay);

		/* Use QSocketNotifier to integrate with Qt event loop. */
		int fd = ConnectionNumber(xiDisplay);
		xiNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
		connect(xiNotifier, &QSocketNotifier::activated, this, &ZoominatorController::processXInput2Events);

		blog(LOG_INFO, "[Zoominator] XInput2 global input hooks installed (XI %d.%d).", major, minor);
	}
#endif
}

void ZoominatorController::uninstallHooks()
{
#ifdef _WIN32
	if (keyboardHook) {
		UnhookWindowsHookEx((HHOOK)keyboardHook);
		keyboardHook = nullptr;
	}
	if (mouseHook) {
		UnhookWindowsHookEx((HHOOK)mouseHook);
		mouseHook = nullptr;
	}
	g_ctl = nullptr;
#elif defined(__APPLE__)
	if (runLoopSource) {
		CFRunLoopRemoveSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopCommonModes);
		CFRelease(runLoopSource);
		runLoopSource = nullptr;
	}
	if (eventTap) {
		CGEventTapEnable(eventTap, false);
		CFRelease(eventTap);
		eventTap = nullptr;
	}
	g_ctl = nullptr;
#elif defined(__linux__)
	if (xiNotifier) {
		delete xiNotifier;
		xiNotifier = nullptr;
	}
	if (xiDisplay) {
		XCloseDisplay(xiDisplay);
		xiDisplay = nullptr;
	}
	g_ctl = nullptr;
#endif
}
