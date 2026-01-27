#include "zoominator-controller.hpp"

#include <obs-frontend-api.h>
#include <obs.h>
#include <util/platform.h>

#include <QApplication>
#include <QKeySequence>
#include <QKeyCombination>
#include <QRegularExpression>

#include <cmath>

#include <QFileInfo>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#endif

#include "zoominator-dialog.hpp"
#include "zoominator-dock.hpp"

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

ZoominatorController::~ZoominatorController() = default;

QString ZoominatorController::configPath() const
{
	char *path = obs_module_config_path("zoominator.json");
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

		const char *dock_id = "zoominator.dock";
		obs_frontend_add_dock_by_id(dock_id, "Zoominator", dock);
	}
}

void ZoominatorController::shutdown()
{
	saveSettings();
	obs_frontend_remove_dock("zoominator.dock");
	uninstallHooks();
	ensureTicking(false);
	resetState();
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
	followSpeed = 8.0;
	portraitCover = true;
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
	if (hotkeyMode != "toggle")
		hotkeyMode = "hold";

	triggerType = getStr("trigger_type");
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
	if (obs_data_has_user_value(data, "follow_speed"))
		followSpeed = obs_data_get_double(data, "follow_speed");
	if (followSpeed <= 0.1)
		followSpeed = 8.0;

	if (obs_data_has_user_value(data, "portrait_cover"))
		portraitCover = obs_data_get_bool(data, "portrait_cover");

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
	obs_data_set_double(data, "follow_speed", followSpeed);
	obs_data_set_bool(data, "portrait_cover", portraitCover);
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
#else
	(void)x;
	(void)y;
	return false;
#endif
}

#ifdef _WIN32
static bool get_monitor_capture_selector(obs_source_t *src, QString &selector, int &monitorId, bool &hasId)
{
	selector.clear();
	monitorId = -1;
	hasId = false;

	obs_data_t *s = obs_source_get_settings(src);
	if (!s)
		return false;

	const char *m = obs_data_get_string(s, "monitor");
	if (m && *m)
		selector = QString::fromUtf8(m);

	if (selector.isEmpty()) {
		monitorId = (int)obs_data_get_int(s, "monitor_id");
		if (monitorId != 0 || obs_data_has_user_value(s, "monitor_id"))
			hasId = true;

		if (!hasId) {
			monitorId = (int)obs_data_get_int(s, "monitor");
			if (monitorId != 0 || obs_data_has_user_value(s, "monitor"))
				hasId = true;
		}
	}

	obs_data_release(s);
	return (!selector.isEmpty()) || hasId;
}

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

static bool match_monitor_rect(obs_source_t *src, RECT &rcOut)
{
	QString selector;
	int monId = -1;
	bool hasId = false;
	if (!get_monitor_capture_selector(src, selector, monId, hasId))
		return false;

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
#endif

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

	if (strcmp(id, "monitor_capture") == 0) {
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
		return false;
	}

	const int w = rc.right - rc.left;
	const int h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0)
		return false;

	if (cursorX < rc.left || cursorX >= rc.right || cursorY < rc.top || cursorY >= rc.bottom) {
		cursorInside = false;
		return true;
	}
	cursorInside = true;

	const double relX = (cursorX - rc.left) / (double)w;
	const double relY = (cursorY - rc.top) / (double)h;

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

void ZoominatorController::applyZoom(obs_sceneitem_t *item, obs_source_t *src, double t)
{
	if (!item || !src || !orig.valid)
		return;

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

	if (followMouse) {
		int cx = 0, cy = 0;
		float mx = 0.f, my = 0.f;
		bool inside = false;
		const bool mapped = getCursorPos(cx, cy) && mapCursorToSourcePixels(src, cx, cy, mx, my, inside);

		if (mapped && inside) {
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
		} else {
			if (followHasPos) {
				fx = followX;
				fy = followY;
			}
		}
	} else {
		if (!followHasPos) {
			int cx = 0, cy = 0;
			float mx = 0.f, my = 0.f;
			bool inside = false;
			const bool mapped = getCursorPos(cx, cy) &&
					    mapCursorToSourcePixels(src, cx, cy, mx, my, inside);
			if (mapped && inside) {
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

	obs_video_info ovi{};
	const bool haveVi = obs_get_video_info(&ovi);
	const double cw = haveVi ? (double)ovi.base_width : 1920.0;
	const double ch = haveVi ? (double)ovi.base_height : 1080.0;

	double cover = 1.0;
	if (portraitCover && ch > cw) {
		const double sx = cw / visW;
		const double sy = ch / visH;
		cover = (sx > sy) ? sx : sy;
	}

	vec2 sc = orig.scale;
	sc.x *= (float)(cover * z);
	sc.y *= (float)(cover * z);
	obs_sceneitem_set_scale(item, &sc);

	const double centerX = cw * 0.5;
	const double centerY = ch * 0.5;
	const double fxAdj = (double)fx - (double)crop.left;
	const double fyAdj = (double)fy - (double)crop.top;
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

	if (animT == 0.0 && animDir == 0) {
		restoreOriginal(item);
		ensureTicking(false);
		resetState();
		return;
	}

	applyZoom(item, src, animT);
}

#ifdef _WIN32
static ZoominatorController *g_ctl = nullptr;

static bool mods_current(bool wantCtrl, bool wantAlt, bool wantShift, bool wantWin)
{
	auto down = [](int vk) {
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
	};
	const bool c = down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL);
	const bool a = down(VK_MENU) || down(VK_LMENU) || down(VK_RMENU);
	const bool s = down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT);
	const bool w = down(VK_LWIN) || down(VK_RWIN);
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

LRESULT CALLBACK ZoominatorController::kb_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && g_ctl && g_ctl->hkValid && g_ctl->triggerType == "keyboard") {
		const auto *k = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
		const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
		const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

		if (k && (down || up) && (int)k->vkCode == g_ctl->hotkeyVk) {
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
	if (nCode == HC_ACTION && g_ctl && g_ctl->triggerType == "mouse") {
		const auto *m = reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
		if (!m)
			return CallNextHookEx((HHOOK)g_ctl->mouseHook, nCode, wParam, lParam);

		const bool down = (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN || wParam == WM_MBUTTONDOWN ||
				   wParam == WM_XBUTTONDOWN);
		const bool up = (wParam == WM_LBUTTONUP || wParam == WM_RBUTTONUP || wParam == WM_MBUTTONUP ||
				 wParam == WM_XBUTTONUP);

		if (down || up) {
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
#endif

bool ZoominatorController::modsMatch() const
{
#ifdef _WIN32
	return mods_current(modCtrl, modAlt, modShift, modWin);
#else
	return true;
#endif
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

static int qtKeyToVk(int qtKey)
{
#ifndef _WIN32
	(void)qtKey;
#endif
#ifdef _WIN32
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
	default:
		break;
	}
#endif
	return 0;
}

void ZoominatorController::rebuildTriggersFromSettings()
{
	hkValid = false;
	hotkeyVk = 0;

	if (triggerType != "mouse")
		triggerType = "keyboard";

	if (triggerType == "keyboard") {
		QKeySequence seq(hotkeySequence);
		if (!seq.isEmpty()) {
			const QKeyCombination kc = seq[0];
			const auto mods = kc.keyboardModifiers();
			const int key = int(kc.key());
			hotkeyVk = qtKeyToVk(key);

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
		} else {
			if (debug)
				blog(LOG_INFO, "[Zoominator] Hotkey empty; keyboard trigger disabled.");
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
#endif
}
