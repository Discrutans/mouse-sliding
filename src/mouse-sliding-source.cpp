#include "mouse-sliding-source.hpp"

#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

constexpr wchar_t kRawMouseClass[] = L"MouseSlidingRawInput";

struct RawMouseListener {
	std::atomic<int> ref_count{0};
	std::atomic<LONG> accum_dx{0};
	std::atomic<LONG> accum_dy{0};
	std::atomic<LONG> accum_wheel{0};
	HANDLE thread = nullptr;
	DWORD thread_id = 0;
	HWND hwnd = nullptr;
	HANDLE ready_event = nullptr;
	bool has_abs = false;
	LONG last_abs_x = 0;
	LONG last_abs_y = 0;
};

RawMouseListener g_raw_mouse;

LRESULT CALLBACK raw_mouse_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	if (msg == WM_INPUT) {
		UINT size = 0;
		GetRawInputData((HRAWINPUT)lparam, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
		if (size == 0)
			return 0;

		BYTE stack_buf[sizeof(RAWINPUT)];
		BYTE *buf = size <= sizeof(stack_buf) ? stack_buf : (BYTE *)malloc(size);
		if (!buf)
			return 0;

		if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == size) {
			auto *raw = (RAWINPUT *)buf;
			if (raw->header.dwType == RIM_TYPEMOUSE) {
				const RAWMOUSE &m = raw->data.mouse;
				LONG dx = 0, dy = 0;
				if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {
					const LONG ax = (LONG)m.lLastX;
					const LONG ay = (LONG)m.lLastY;
					if (g_raw_mouse.has_abs) {
						dx = ax - g_raw_mouse.last_abs_x;
						dy = ay - g_raw_mouse.last_abs_y;
					}
					g_raw_mouse.last_abs_x = ax;
					g_raw_mouse.last_abs_y = ay;
					g_raw_mouse.has_abs = true;
				} else {
					dx = m.lLastX;
					dy = m.lLastY;
					g_raw_mouse.has_abs = false;
				}
				if (dx != 0)
					g_raw_mouse.accum_dx.fetch_add(dx, std::memory_order_relaxed);
				if (dy != 0)
					g_raw_mouse.accum_dy.fetch_add(dy, std::memory_order_relaxed);

				if (m.usButtonFlags & RI_MOUSE_WHEEL) {
					const SHORT delta = (SHORT)m.usButtonData;
					g_raw_mouse.accum_wheel.fetch_add((LONG)delta, std::memory_order_relaxed);
				}
				if (m.usButtonFlags & RI_MOUSE_HWHEEL) {
					const SHORT delta = (SHORT)m.usButtonData;
					g_raw_mouse.accum_wheel.fetch_add((LONG)delta, std::memory_order_relaxed);
				}
			}
		}

		if (buf != stack_buf)
			free(buf);
		return 0;
	}

	if (msg == WM_DESTROY) {
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

DWORD WINAPI raw_mouse_thread_main(LPVOID)
{
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = raw_mouse_wnd_proc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = kRawMouseClass;
	if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
		if (g_raw_mouse.ready_event)
			SetEvent(g_raw_mouse.ready_event);
		return 1;
	}

	g_raw_mouse.hwnd = CreateWindowExW(0, kRawMouseClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
					   GetModuleHandleW(nullptr), nullptr);
	if (!g_raw_mouse.hwnd) {
		if (g_raw_mouse.ready_event)
			SetEvent(g_raw_mouse.ready_event);
		return 1;
	}

	RAWINPUTDEVICE rid = {};
	rid.usUsagePage = 0x01;
	rid.usUsage = 0x02;
	rid.dwFlags = RIDEV_INPUTSINK;
	rid.hwndTarget = g_raw_mouse.hwnd;
	if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
		DestroyWindow(g_raw_mouse.hwnd);
		g_raw_mouse.hwnd = nullptr;
		if (g_raw_mouse.ready_event)
			SetEvent(g_raw_mouse.ready_event);
		return 1;
	}

	if (g_raw_mouse.ready_event)
		SetEvent(g_raw_mouse.ready_event);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	rid.dwFlags = RIDEV_REMOVE;
	rid.hwndTarget = nullptr;
	RegisterRawInputDevices(&rid, 1, sizeof(rid));

	if (g_raw_mouse.hwnd) {
		DestroyWindow(g_raw_mouse.hwnd);
		g_raw_mouse.hwnd = nullptr;
	}

	return 0;
}

void raw_mouse_add_ref()
{
	if (g_raw_mouse.ref_count.fetch_add(1, std::memory_order_acq_rel) == 0) {
		g_raw_mouse.accum_dx.store(0, std::memory_order_relaxed);
		g_raw_mouse.accum_dy.store(0, std::memory_order_relaxed);
		g_raw_mouse.accum_wheel.store(0, std::memory_order_relaxed);
		g_raw_mouse.has_abs = false;
		g_raw_mouse.ready_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		g_raw_mouse.thread = CreateThread(nullptr, 0, raw_mouse_thread_main, nullptr, 0, &g_raw_mouse.thread_id);
		if (g_raw_mouse.thread && g_raw_mouse.ready_event)
			WaitForSingleObject(g_raw_mouse.ready_event, 2000);
		if (g_raw_mouse.ready_event) {
			CloseHandle(g_raw_mouse.ready_event);
			g_raw_mouse.ready_event = nullptr;
		}
	}
}

void raw_mouse_release()
{
	if (g_raw_mouse.ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
		if (g_raw_mouse.hwnd && g_raw_mouse.thread_id)
			PostThreadMessageW(g_raw_mouse.thread_id, WM_QUIT, 0, 0);
		if (g_raw_mouse.thread) {
			WaitForSingleObject(g_raw_mouse.thread, 3000);
			CloseHandle(g_raw_mouse.thread);
			g_raw_mouse.thread = nullptr;
			g_raw_mouse.thread_id = 0;
		}
		g_raw_mouse.accum_dx.store(0, std::memory_order_relaxed);
		g_raw_mouse.accum_dy.store(0, std::memory_order_relaxed);
		g_raw_mouse.accum_wheel.store(0, std::memory_order_relaxed);
		g_raw_mouse.has_abs = false;
	}
}

void raw_mouse_consume(int *dx, int *dy, int *wheel)
{
	*dx = (int)g_raw_mouse.accum_dx.exchange(0, std::memory_order_acq_rel);
	*dy = (int)g_raw_mouse.accum_dy.exchange(0, std::memory_order_acq_rel);
	*wheel = (int)g_raw_mouse.accum_wheel.exchange(0, std::memory_order_acq_rel);
}

}
#endif

constexpr int kCircleSegments = 24;
constexpr int kWheelArcSegments = 10;
constexpr int kMaxClickRings = 4;
constexpr int kMaxTrail = 64;
constexpr float kMarginRatio = 0.08f;
constexpr float kTrailMinMove = 0.75f;
constexpr float kEdgeOverflowRef = 48.0f;
constexpr float kAttractEdgeHold = 0.35f;
constexpr float kWheelRadPerNotch = 0.38f;
constexpr float kWheelOmegaPerNotch = 6.5f;
constexpr float kWheelDelta = 120.0f;
constexpr int kFontCell = 16;
constexpr int kFontCols = 8;
constexpr char kFontChars[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr int kFontCharCount = (int)(sizeof(kFontChars) - 1);
constexpr char kGitHubRepoUrl[] = "https://github.com/Discrutans/mouse-sliding";
constexpr char kSupportUrl[] = "https://dalink.to/discrutans";

struct TrailPoint {
	float x = 0.0f;
	float y = 0.0f;
	float speed = 0.0f;
};

struct ClickRing {
	float x = 0.0f;
	float y = 0.0f;
	float life = 0.0f;
	vec4 color = {};
	bool active = false;
};

struct MouseSlidingData {
	obs_source_t *source = nullptr;

	float canvas_w = 400.0f;
	float canvas_h = 400.0f;

	uint32_t color_idle = 0;
	uint32_t color_lmb = 0;
	uint32_t color_rmb = 0;
	uint32_t color_trail_slow = 0;
	uint32_t color_trail_fast = 0;
	uint32_t combo_color = 0;
	uint32_t combo_color_hot = 0;
	int combo_hot_threshold = 10;

	float radius_idle = 20.0f;
	float radius_click = 15.0f;
	float pad_sensitivity = 1.0f;
	float pad_follow = 0.55f;
	float glow_strength = 0.45f;
	float trail_width = 14.0f;
	int trail_length = 16;

	bool show_pad_frame = true;
	bool show_axes = true;
	bool show_grid = false;
	bool request_recenter = false;

	bool attract_enabled = false;
	bool attract_on_idle = true;
	bool attract_on_edge = false;
	float attract_idle_sec = 1.5f;
	float attract_strength = 0.35f;
	float attract_idle_timer = 0.0f;
	float attract_edge_hold = 0.0f;
	bool attracting = false;

	float edge_parallax = 12.0f;
	float edge_glow = 0.55f;
	float edge_ox = 0.0f;
	float edge_oy = 0.0f;
	float edge_intensity = 0.0f;
	bool edge_hit_l = false;
	bool edge_hit_r = false;
	bool edge_hit_t = false;
	bool edge_hit_b = false;

	bool show_wheel = true;
	float wheel_scale = 1.0f;
	float wheel_angle = 0.0f;
	float wheel_omega = 0.0f;
	float wheel_alpha = 0.0f;
	float wheel_pop = 0.0f;
	float wheel_spin_sign = 1.0f;

	bool combo_enabled = true;
	float combo_window = 0.40f;
	float hold_fire_rate = 10.0f;
	float combo_scale = 1.2f;
	float combo_offset_y = 42.0f;
	char combo_font[32] = "arcade";
	char combo_text_single[48] = "HIT";
	char combo_text_multi[48] = "{n} HIT COMBO";

	float curr_x = 200.0f;
	float curr_y = 200.0f;
	float pad_x = 200.0f;
	float pad_y = 200.0f;
	float curr_speed = 0.0f;
	float curr_radius = 20.0f;
	vec4 curr_color = {};

	bool prev_lmb = false;
	bool prev_rmb = false;

	int combo_count = 0;
	float time_since_hit = 999.0f;
	float hold_accum = 0.0f;
	float label_pop = 0.0f;
	float label_alpha = 0.0f;
	char combo_text[64] = {};

	gs_image_file_t font_image = {};
	gs_texture_t *font_tex = nullptr;
	bool font_loaded = false;
	gs_effect_t *tint_effect = nullptr;

	TrailPoint trail[kMaxTrail];
	int trail_head = 0;
	int trail_count = 0;

	ClickRing rings[kMaxClickRings];
};

static float canvas_half_x(const MouseSlidingData *data)
{
	return data->canvas_w * 0.5f;
}

static float canvas_half_y(const MouseSlidingData *data)
{
	return data->canvas_h * 0.5f;
}

static float clamp_margin_x(const MouseSlidingData *data)
{
	return data->canvas_w * kMarginRatio;
}

static float clamp_margin_y(const MouseSlidingData *data)
{
	return data->canvas_h * kMarginRatio;
}

static float lerp_factor(float amount, float seconds)
{
	amount = std::clamp(amount, 0.0f, 1.0f);
	if (amount <= 0.0f)
		return 0.0f;
	if (amount >= 1.0f)
		return 1.0f;
	return 1.0f - std::pow(1.0f - amount, seconds * 60.0f);
}

static void color_from_obs(uint32_t obs_color, vec4 *out)
{
	vec4_from_rgba(out, obs_color);
	if (out->w <= 0.0f)
		out->w = 1.0f;
}

static void lerp_vec4(vec4 *dst, const vec4 *target, float t)
{
	dst->x += (target->x - dst->x) * t;
	dst->y += (target->y - dst->y) * t;
	dst->z += (target->z - dst->z) * t;
	dst->w += (target->w - dst->w) * t;
}

static void mix_vec4(vec4 *out, const vec4 *a, const vec4 *b, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	out->x = a->x + (b->x - a->x) * t;
	out->y = a->y + (b->y - a->y) * t;
	out->z = a->z + (b->z - a->z) * t;
	out->w = a->w + (b->w - a->w) * t;
}

static void trail_clear(MouseSlidingData *data)
{
	data->trail_head = 0;
	data->trail_count = 0;
}

static void trail_push(MouseSlidingData *data, float x, float y, float speed)
{
	const int cap = std::clamp(data->trail_length, 0, kMaxTrail);
	if (cap <= 0) {
		trail_clear(data);
		return;
	}
	if (data->trail_count > 0) {
		const int last = (data->trail_head - 1 + kMaxTrail) % kMaxTrail;
		const float dx = x - data->trail[last].x;
		const float dy = y - data->trail[last].y;
		if (dx * dx + dy * dy < kTrailMinMove * kTrailMinMove)
			return;
	}
	data->trail[data->trail_head] = {x, y, speed};
	data->trail_head = (data->trail_head + 1) % kMaxTrail;
	if (data->trail_count < cap)
		data->trail_count++;
	else if (data->trail_count > cap)
		data->trail_count = cap;
}

static TrailPoint trail_at(const MouseSlidingData *data, int i)
{
	const int idx = (data->trail_head - data->trail_count + i + kMaxTrail * 2) % kMaxTrail;
	return data->trail[idx];
}

static void reset_motion_state(MouseSlidingData *data)
{
	data->curr_x = canvas_half_x(data);
	data->curr_y = canvas_half_y(data);
	data->pad_x = data->curr_x;
	data->pad_y = data->curr_y;
	data->curr_speed = 0.0f;
	data->edge_ox = 0.0f;
	data->edge_oy = 0.0f;
	data->edge_intensity = 0.0f;
	data->edge_hit_l = data->edge_hit_r = data->edge_hit_t = data->edge_hit_b = false;
	data->attract_idle_timer = 0.0f;
	data->attract_edge_hold = 0.0f;
	data->attracting = false;
	data->wheel_omega = 0.0f;
	data->wheel_alpha = 0.0f;
	data->wheel_pop = 0.0f;
	trail_clear(data);
}

static void sanitize_atlas_text(char *dst, size_t dst_sz, const char *src)
{
	size_t o = 0;
	for (size_t i = 0; src[i] && o + 1 < dst_sz; i++) {
		char c = src[i];
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');
		bool ok = (c == ' ') || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
		dst[o++] = ok ? c : ' ';
	}
	dst[o] = '\0';
}

static void apply_template(char *dst, size_t dst_sz, const char *tmpl, int n)
{
	char raw[96];
	raw[0] = '\0';
	const char *p = tmpl ? tmpl : "";
	size_t o = 0;
	while (*p && o + 1 < sizeof(raw)) {
		if (p[0] == '{' && p[1] == 'n' && p[2] == '}') {
			char num[16];
			snprintf(num, sizeof(num), "%d", n);
			for (int i = 0; num[i] && o + 1 < sizeof(raw); i++)
				raw[o++] = num[i];
			p += 3;
		} else {
			raw[o++] = *p++;
		}
	}
	raw[o] = '\0';
	sanitize_atlas_text(dst, dst_sz, raw);
}

static void update_combo_text(MouseSlidingData *data)
{
	if (data->combo_count <= 0) {
		data->combo_text[0] = '\0';
		return;
	}
	if (data->combo_count == 1)
		apply_template(data->combo_text, sizeof(data->combo_text), data->combo_text_single, 1);
	else
		apply_template(data->combo_text, sizeof(data->combo_text), data->combo_text_multi, data->combo_count);
}

static void register_combo_hit(MouseSlidingData *data)
{
	data->combo_count++;
	if (data->combo_count > 999)
		data->combo_count = 999;
	data->time_since_hit = 0.0f;
	data->label_pop = 1.0f;
	data->label_alpha = 1.0f;
	update_combo_text(data);
}

static void spawn_ring(MouseSlidingData *data, float x, float y, uint32_t color)
{
	for (int i = 0; i < kMaxClickRings; i++) {
		if (!data->rings[i].active) {
			data->rings[i].active = true;
			data->rings[i].x = x;
			data->rings[i].y = y;
			data->rings[i].life = 1.0f;
			color_from_obs(color, &data->rings[i].color);
			return;
		}
	}
	int oldest = 0;
	for (int i = 1; i < kMaxClickRings; i++) {
		if (data->rings[i].life < data->rings[oldest].life)
			oldest = i;
	}
	data->rings[oldest].active = true;
	data->rings[oldest].x = x;
	data->rings[oldest].y = y;
	data->rings[oldest].life = 1.0f;
	color_from_obs(color, &data->rings[oldest].color);
}

static void unload_font(MouseSlidingData *data)
{
	obs_enter_graphics();
	gs_image_file_free(&data->font_image);
	obs_leave_graphics();
	data->font_tex = nullptr;
	data->font_loaded = false;
}

static void load_font(MouseSlidingData *data)
{
	unload_font(data);

	char rel[96];
	const char *id = data->combo_font[0] ? data->combo_font : "arcade";
	if (strcmp(id, "block") != 0 && strcmp(id, "slim") != 0)
		id = "arcade";
	snprintf(rel, sizeof(rel), "fonts/%s_atlas.png", id);

	char *path = obs_module_file(rel);
	if (!path)
		return;

	obs_enter_graphics();
	gs_image_file_init(&data->font_image, path);
	gs_image_file_init_texture(&data->font_image);
	data->font_tex = data->font_image.texture;
	data->font_loaded = data->font_tex != nullptr;
	obs_leave_graphics();
	bfree(path);
}

static void load_tint_effect(MouseSlidingData *data)
{
	if (data->tint_effect)
		return;
	char *path = obs_module_file("effects/text_tint.effect");
	if (!path)
		return;
	char *error = nullptr;
	obs_enter_graphics();
	data->tint_effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (error) {
		blog(LOG_WARNING, "[mouse-sliding] tint effect: %s", error);
		bfree(error);
	}
	bfree(path);
}

static void unload_tint_effect(MouseSlidingData *data)
{
	if (!data->tint_effect)
		return;
	obs_enter_graphics();
	gs_effect_destroy(data->tint_effect);
	obs_leave_graphics();
	data->tint_effect = nullptr;
}

static int glyph_index(char c)
{
	if (c >= 'a' && c <= 'z')
		c = (char)(c - 'a' + 'A');
	for (int i = 0; i < kFontCharCount; i++) {
		if (kFontChars[i] == c)
			return i;
	}
	return 0;
}

static void draw_filled_circle(float cx, float cy, float radius)
{
	if (radius <= 0.05f)
		return;
	gs_render_start(true);
	for (int i = 0; i < kCircleSegments; i++) {
		const float a0 = (float)i * (2.0f * (float)M_PI / (float)kCircleSegments);
		const float a1 = (float)(i + 1) * (2.0f * (float)M_PI / (float)kCircleSegments);
		gs_vertex2f(cx, cy);
		gs_vertex2f(cx + cosf(a0) * radius, cy + sinf(a0) * radius);
		gs_vertex2f(cx + cosf(a1) * radius, cy + sinf(a1) * radius);
	}
	gs_render_stop(GS_TRIS);
}

static void draw_circle_outline(float cx, float cy, float radius, float thickness)
{
	if (radius <= 0.05f || thickness <= 0.05f)
		return;
	const float inner = std::max(0.0f, radius - thickness * 0.5f);
	const float outer = radius + thickness * 0.5f;
	gs_render_start(true);
	for (int i = 0; i < kCircleSegments; i++) {
		const float a0 = (float)i * (2.0f * (float)M_PI / (float)kCircleSegments);
		const float a1 = (float)(i + 1) * (2.0f * (float)M_PI / (float)kCircleSegments);
		const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
		gs_vertex2f(cx + c0 * inner, cy + s0 * inner);
		gs_vertex2f(cx + c0 * outer, cy + s0 * outer);
		gs_vertex2f(cx + c1 * outer, cy + s1 * outer);
		gs_vertex2f(cx + c0 * inner, cy + s0 * inner);
		gs_vertex2f(cx + c1 * outer, cy + s1 * outer);
		gs_vertex2f(cx + c1 * inner, cy + s1 * inner);
	}
	gs_render_stop(GS_TRIS);
}

static void draw_line_segment(float x0, float y0, float x1, float y1, float thickness)
{
	float dx = x1 - x0, dy = y1 - y0;
	const float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f)
		return;
	dx /= len;
	dy /= len;
	const float px = -dy * thickness * 0.5f;
	const float py = dx * thickness * 0.5f;
	gs_render_start(true);
	gs_vertex2f(x0 + px, y0 + py);
	gs_vertex2f(x0 - px, y0 - py);
	gs_vertex2f(x1 - px, y1 - py);
	gs_vertex2f(x0 + px, y0 + py);
	gs_vertex2f(x1 - px, y1 - py);
	gs_vertex2f(x1 + px, y1 + py);
	gs_render_stop(GS_TRIS);
}

static void draw_rect_outline(float x0, float y0, float x1, float y1, float thickness)
{
	draw_line_segment(x0, y0, x1, y0, thickness);
	draw_line_segment(x1, y0, x1, y1, thickness);
	draw_line_segment(x1, y1, x0, y1, thickness);
	draw_line_segment(x0, y1, x0, y0, thickness);
}

static void draw_arc_outline(float cx, float cy, float radius, float thickness, float a0, float a1, int segs)
{
	if (radius <= 0.05f || thickness <= 0.05f || segs < 2)
		return;
	const float span = a1 - a0;
	for (int i = 0; i < segs; i++) {
		const float t0 = (float)i / (float)segs;
		const float t1 = (float)(i + 1) / (float)segs;
		const float ang0 = a0 + span * t0;
		const float ang1 = a0 + span * t1;
		draw_line_segment(cx + cosf(ang0) * radius, cy + sinf(ang0) * radius, cx + cosf(ang1) * radius,
				  cy + sinf(ang1) * radius, thickness);
	}
}

static void draw_ribbon_batched(const MouseSlidingData *data, float base_width, float speed_ref,
				const vec4 *color_slow, const vec4 *color_fast, gs_effect_t *solid)
{
	const int count = data->trail_count;
	if (count < 2 || base_width <= 0.1f || !solid)
		return;

	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
	vec4 one;
	vec4_set(&one, 1.0f, 1.0f, 1.0f, 1.0f);
	gs_effect_set_vec4(color_param, &one);

	while (gs_effect_loop(solid, "SolidColored")) {
		gs_render_start(true);
		for (int i = 0; i < count - 1; i++) {
			const TrailPoint a = trail_at(data, i);
			const TrailPoint b = trail_at(data, i + 1);
			float dx = b.x - a.x, dy = b.y - a.y;
			float len = sqrtf(dx * dx + dy * dy);
			if (len < 0.01f)
				continue;
			dx /= len;
			dy /= len;

			const float t0 = (float)i / (float)(count - 1);
			const float t1 = (float)(i + 1) / (float)(count - 1);
			const float fade0 = t0 * t0;
			const float fade1 = t1 * t1;
			const float w0 = base_width * (0.15f + 0.85f * fade0) * 0.5f;
			const float w1 = base_width * (0.15f + 0.85f * fade1) * 0.5f;
			const float px = -dy, py = dx;

			const float speed_t =
				std::clamp(((a.speed + b.speed) * 0.5f) / std::max(1.0f, speed_ref), 0.0f, 1.0f);
			vec4 col;
			mix_vec4(&col, color_slow, color_fast, speed_t);
			col.w *= (0.08f + 0.62f * fade1);
			const uint32_t rgba = vec4_to_rgba(&col);

			gs_color(rgba);
			gs_vertex2f(a.x + px * w0, a.y + py * w0);
			gs_color(rgba);
			gs_vertex2f(a.x - px * w0, a.y - py * w0);
			gs_color(rgba);
			gs_vertex2f(b.x - px * w1, b.y - py * w1);

			gs_color(rgba);
			gs_vertex2f(a.x + px * w0, a.y + py * w0);
			gs_color(rgba);
			gs_vertex2f(b.x - px * w1, b.y - py * w1);
			gs_color(rgba);
			gs_vertex2f(b.x + px * w1, b.y + py * w1);
		}
		gs_render_stop(GS_TRIS);
	}
}

static void draw_glyph_quad(float x, float y, float size, int glyph, float tex_w, float tex_h)
{
	const int col = glyph % kFontCols;
	const int row = glyph / kFontCols;
	const float u0 = (float)(col * kFontCell) / tex_w;
	const float v0 = (float)(row * kFontCell) / tex_h;
	const float u1 = (float)((col + 1) * kFontCell) / tex_w;
	const float v1 = (float)((row + 1) * kFontCell) / tex_h;

	gs_texcoord(u0, v0, 0);
	gs_vertex2f(x, y);
	gs_texcoord(u1, v0, 0);
	gs_vertex2f(x + size, y);
	gs_texcoord(u1, v1, 0);
	gs_vertex2f(x + size, y + size);

	gs_texcoord(u0, v0, 0);
	gs_vertex2f(x, y);
	gs_texcoord(u1, v1, 0);
	gs_vertex2f(x + size, y + size);
	gs_texcoord(u0, v1, 0);
	gs_vertex2f(x, y + size);
}

static void draw_combo_label(MouseSlidingData *data)
{
	if (!data->combo_enabled || data->combo_count <= 0 || data->label_alpha <= 0.01f)
		return;
	if (!data->font_loaded || !data->font_tex || !data->tint_effect)
		return;

	const char *text = data->combo_text;
	const int len = (int)strlen(text);
	if (len <= 0)
		return;

	const float pop = 1.0f + data->label_pop * 0.15f;
	const float big = data->combo_count >= data->combo_hot_threshold ? 1.12f : 1.0f;
	const float glyph = kFontCell * data->combo_scale * pop * big;
	const float total_w = glyph * (float)len;
	const float shake = data->label_pop * 2.0f * sinf(data->label_pop * 40.0f);
	const float x0 = data->curr_x - total_w * 0.5f;
	const float y0 = data->curr_y - data->combo_offset_y - glyph * 0.5f + shake;

	vec4 base_c, hot_c, tint;
	color_from_obs(data->combo_color, &base_c);
	color_from_obs(data->combo_color_hot, &hot_c);
	if (data->combo_count >= data->combo_hot_threshold)
		tint = hot_c;
	else
		tint = base_c;
	tint.w *= data->label_alpha;

	const uint32_t tw = gs_texture_get_width(data->font_tex);
	const uint32_t th = gs_texture_get_height(data->font_tex);

	gs_effect_t *effect = data->tint_effect;
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_eparam_t *color = gs_effect_get_param_by_name(effect, "color");
	gs_effect_set_texture(image, data->font_tex);
	gs_effect_set_vec4(color, &tint);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
	while (gs_effect_loop(effect, "Draw")) {
		gs_render_start(true);
		for (int i = 0; i < len; i++)
			draw_glyph_quad(x0 + glyph * (float)i, y0, glyph, glyph_index(text[i]), (float)tw, (float)th);
		gs_render_stop(GS_TRIS);
	}
	gs_blend_state_pop();
}

static const char *sliding_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("MouseSliding");
}

static bool sliding_recenter_clicked(obs_properties_t *props, obs_property_t *property, void *data_ptr)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);
	auto *data = static_cast<MouseSlidingData *>(data_ptr);
	if (data)
		data->request_recenter = true;
	return false;
}

static void sliding_update(void *data_ptr, obs_data_t *settings)
{
	auto *data = static_cast<MouseSlidingData *>(data_ptr);

	const float new_w = (float)obs_data_get_double(settings, "canvas_width");
	const float new_h = (float)obs_data_get_double(settings, "canvas_height");
	const bool canvas_changed =
		fabsf(new_w - data->canvas_w) > 0.5f || fabsf(new_h - data->canvas_h) > 0.5f;
	data->canvas_w = std::clamp(new_w, 128.0f, 1920.0f);
	data->canvas_h = std::clamp(new_h, 128.0f, 1080.0f);

	data->color_idle = (uint32_t)obs_data_get_int(settings, "color_idle");
	data->color_lmb = (uint32_t)obs_data_get_int(settings, "color_lmb");
	data->color_rmb = (uint32_t)obs_data_get_int(settings, "color_rmb");
	data->color_trail_slow = (uint32_t)obs_data_get_int(settings, "color_trail_slow");
	data->color_trail_fast = (uint32_t)obs_data_get_int(settings, "color_trail_fast");
	data->combo_color = (uint32_t)obs_data_get_int(settings, "combo_color");
	data->combo_color_hot = (uint32_t)obs_data_get_int(settings, "combo_color_hot");
	data->combo_hot_threshold = (int)obs_data_get_int(settings, "combo_hot_threshold");

	data->radius_idle = (float)obs_data_get_double(settings, "radius_idle");
	data->radius_click = (float)obs_data_get_double(settings, "radius_click");
	data->pad_sensitivity = (float)obs_data_get_double(settings, "pad_sensitivity");
	data->pad_follow = (float)obs_data_get_double(settings, "pad_follow");
	data->glow_strength = (float)obs_data_get_double(settings, "glow_strength");
	data->trail_width = (float)obs_data_get_double(settings, "trail_width");
	data->trail_length = std::clamp((int)obs_data_get_int(settings, "trail_length"), 0, kMaxTrail);

	data->show_pad_frame = obs_data_get_bool(settings, "show_pad_frame");
	data->show_axes = obs_data_get_bool(settings, "show_axes");
	data->show_grid = obs_data_get_bool(settings, "show_grid");
	data->edge_parallax = (float)obs_data_get_double(settings, "edge_parallax");
	data->edge_glow = (float)obs_data_get_double(settings, "edge_glow");
	data->show_wheel = obs_data_get_bool(settings, "show_wheel");
	data->wheel_scale = (float)obs_data_get_double(settings, "wheel_scale");

	data->attract_enabled = obs_data_get_bool(settings, "attract_enabled");
	data->attract_on_idle = obs_data_get_bool(settings, "attract_on_idle");
	data->attract_on_edge = obs_data_get_bool(settings, "attract_on_edge");
	data->attract_idle_sec = (float)obs_data_get_double(settings, "attract_idle_sec");
	data->attract_strength = (float)obs_data_get_double(settings, "attract_strength");

	data->combo_enabled = obs_data_get_bool(settings, "combo_enabled");
	data->combo_window = (float)obs_data_get_double(settings, "combo_window");
	data->hold_fire_rate = (float)obs_data_get_double(settings, "hold_fire_rate");
	data->combo_scale = (float)obs_data_get_double(settings, "combo_scale");
	data->combo_offset_y = (float)obs_data_get_double(settings, "combo_offset_y");

	const char *font = obs_data_get_string(settings, "combo_font");
	char new_font[32];
	snprintf(new_font, sizeof(new_font), "%s", (font && font[0]) ? font : "arcade");
	const bool font_changed = strcmp(new_font, data->combo_font) != 0;
	snprintf(data->combo_font, sizeof(data->combo_font), "%s", new_font);

	const char *single = obs_data_get_string(settings, "combo_text_single");
	const char *multi = obs_data_get_string(settings, "combo_text_multi");
	snprintf(data->combo_text_single, sizeof(data->combo_text_single), "%s",
		 (single && single[0]) ? single : "HIT");
	snprintf(data->combo_text_multi, sizeof(data->combo_text_multi), "%s",
		 (multi && multi[0]) ? multi : "{n} HIT COMBO");

	if (data->trail_count > data->trail_length)
		data->trail_count = data->trail_length;

	if (canvas_changed)
		reset_motion_state(data);
	if (font_changed)
		load_font(data);

	update_combo_text(data);
}

static void *sliding_create(obs_data_t *settings, obs_source_t *source)
{
	auto *data = new MouseSlidingData();
	data->source = source;
	color_from_obs(0xFFFFFF00, &data->curr_color);

#ifdef _WIN32
	raw_mouse_add_ref();
#endif

	sliding_update(data, settings);
	load_font(data);
	load_tint_effect(data);
	return data;
}

static void sliding_destroy(void *data_ptr)
{
	auto *data = static_cast<MouseSlidingData *>(data_ptr);
	unload_font(data);
	unload_tint_effect(data);
#ifdef _WIN32
	raw_mouse_release();
#endif
	delete data;
}

static uint32_t sliding_get_width(void *data_ptr)
{
	return (uint32_t)static_cast<MouseSlidingData *>(data_ptr)->canvas_w;
}

static uint32_t sliding_get_height(void *data_ptr)
{
	return (uint32_t)static_cast<MouseSlidingData *>(data_ptr)->canvas_h;
}

static void sliding_video_tick(void *data_ptr, float seconds)
{
	auto *data = static_cast<MouseSlidingData *>(data_ptr);
	if (seconds <= 0.0f)
		seconds = 1.0f / 60.0f;
	seconds = std::min(seconds, 0.05f);

	const bool showing = obs_source_showing(data->source);

	int dx = 0, dy = 0, wheel = 0;
	bool lmb = false, rmb = false;

#ifdef _WIN32
	raw_mouse_consume(&dx, &dy, &wheel);
	lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
#endif

	if (data->request_recenter) {
		reset_motion_state(data);
		data->request_recenter = false;
	}

	const float margin_x = clamp_margin_x(data);
	const float margin_y = clamp_margin_y(data);
	const float min_x = margin_x;
	const float max_x = data->canvas_w - margin_x;
	const float min_y = margin_y;
	const float max_y = data->canvas_h - margin_y;
	const float move_x = (float)dx;
	const float move_y = (float)dy;
	const float frame_speed = sqrtf(move_x * move_x + move_y * move_y) / std::max(seconds, 0.0001f);

	const float desired_x = data->pad_x + move_x * data->pad_sensitivity;
	const float desired_y = data->pad_y + move_y * data->pad_sensitivity;
	data->pad_x = std::clamp(desired_x, min_x, max_x);
	data->pad_y = std::clamp(desired_y, min_y, max_y);
	const float overflow_x = desired_x - data->pad_x;
	const float overflow_y = desired_y - data->pad_y;

	const float edge_decay = 1.0f - lerp_factor(0.18f, seconds);
	const float edge_follow = lerp_factor(0.28f, seconds);
	const float parallax = std::max(0.0f, data->edge_parallax);
	float target_ox = std::clamp(overflow_x * 0.55f, -parallax, parallax);
	float target_oy = std::clamp(overflow_y * 0.55f, -parallax, parallax);
	if (fabsf(overflow_x) < 0.05f)
		target_ox = data->edge_ox * edge_decay;
	if (fabsf(overflow_y) < 0.05f)
		target_oy = data->edge_oy * edge_decay;
	data->edge_ox += (target_ox - data->edge_ox) * edge_follow;
	data->edge_oy += (target_oy - data->edge_oy) * edge_follow;
	data->edge_ox = std::clamp(data->edge_ox, -parallax, parallax);
	data->edge_oy = std::clamp(data->edge_oy, -parallax, parallax);

	const float at_eps = 0.35f;
	const bool at_l = data->pad_x <= min_x + at_eps;
	const bool at_r = data->pad_x >= max_x - at_eps;
	const bool at_t = data->pad_y <= min_y + at_eps;
	const bool at_b = data->pad_y >= max_y - at_eps;
	const bool at_edge = at_l || at_r || at_t || at_b;

	data->edge_hit_l = at_l && (data->edge_ox < -0.4f || overflow_x < -0.05f);
	data->edge_hit_r = at_r && (data->edge_ox > 0.4f || overflow_x > 0.05f);
	data->edge_hit_t = at_t && (data->edge_oy < -0.4f || overflow_y < -0.05f);
	data->edge_hit_b = at_b && (data->edge_oy > 0.4f || overflow_y > 0.05f);
	const float edge_mag = sqrtf(data->edge_ox * data->edge_ox + data->edge_oy * data->edge_oy);
	data->edge_intensity = std::clamp(edge_mag / std::max(1.0f, parallax > 0.1f ? parallax : kEdgeOverflowRef),
					  0.0f, 1.0f);

	data->attracting = false;
	if (data->attract_enabled) {
		constexpr float kMoveEps = 0.5f;
		const bool mouse_active = (fabsf(move_x) + fabsf(move_y)) > kMoveEps;
		if (mouse_active || lmb || rmb)
			data->attract_idle_timer = 0.0f;
		else
			data->attract_idle_timer += seconds;

		const bool overflow_wall = (at_l && overflow_x < -0.05f) || (at_r && overflow_x > 0.05f) ||
					   (at_t && overflow_y < -0.05f) || (at_b && overflow_y > 0.05f);
		if (data->attract_on_edge && (at_edge || overflow_wall))
			data->attract_edge_hold = kAttractEdgeHold;
		else
			data->attract_edge_hold = std::max(0.0f, data->attract_edge_hold - seconds);

		const bool do_idle = data->attract_on_idle && data->attract_idle_timer >= data->attract_idle_sec &&
				     !mouse_active;
		const bool do_edge = data->attract_on_edge && data->attract_edge_hold > 0.0f;
		if (do_idle || do_edge) {
			data->attracting = true;
			const float cx = data->canvas_w * 0.5f;
			const float cy = data->canvas_h * 0.5f;
			const float at = lerp_factor(std::clamp(data->attract_strength, 0.0f, 1.0f), seconds);
			data->pad_x += (cx - data->pad_x) * at;
			data->pad_y += (cy - data->pad_y) * at;
		}
	} else {
		data->attract_edge_hold = 0.0f;
	}

	if (wheel != 0) {
		const float notches = (float)wheel / kWheelDelta;
		data->wheel_spin_sign = notches >= 0.0f ? 1.0f : -1.0f;
		data->wheel_angle += notches * kWheelRadPerNotch;
		data->wheel_omega += notches * kWheelOmegaPerNotch;
		data->wheel_alpha = 1.0f;
		data->wheel_pop = 1.0f;
	}
	data->wheel_angle += data->wheel_omega * seconds;
	const float omega_decay = powf(0.88f, seconds * 60.0f);
	data->wheel_omega *= omega_decay;
	data->wheel_alpha = std::max(0.0f, data->wheel_alpha - seconds / 0.45f);
	data->wheel_pop = std::max(0.0f, data->wheel_pop - seconds / 0.12f);

	const float follow_t = lerp_factor(data->pad_follow, seconds);
	data->curr_x += (data->pad_x - data->curr_x) * follow_t;
	data->curr_y += (data->pad_y - data->curr_y) * follow_t;
	data->curr_speed = frame_speed * 0.02f;

	const bool clicking = lmb || rmb;
	const bool lmb_edge = lmb && !data->prev_lmb;
	const bool rmb_edge = rmb && !data->prev_rmb;

	if (lmb_edge)
		spawn_ring(data, data->curr_x, data->curr_y, data->color_lmb);
	if (rmb_edge)
		spawn_ring(data, data->curr_x, data->curr_y, data->color_rmb);

	if (data->combo_enabled) {
		data->time_since_hit += seconds;
		data->label_pop = std::max(0.0f, data->label_pop - seconds * 4.0f);

		if (lmb_edge || rmb_edge) {
			register_combo_hit(data);
			data->hold_accum = 0.0f;
		}

		if (clicking) {
			const float interval = 1.0f / std::max(0.5f, data->hold_fire_rate);
			data->hold_accum += seconds;
			while (data->hold_accum >= interval) {
				data->hold_accum -= interval;
				if (!lmb_edge && !rmb_edge)
					register_combo_hit(data);
			}
		} else {
			data->hold_accum = 0.0f;
		}

		if (data->combo_count > 0 && data->time_since_hit > data->combo_window) {
			data->combo_count = 0;
			data->combo_text[0] = '\0';
			data->label_alpha = 0.0f;
			data->hold_accum = 0.0f;
		} else if (data->combo_count > 0) {
			const float remain = data->combo_window - data->time_since_hit;
			data->label_alpha = std::clamp(remain / std::max(0.12f, data->combo_window * 0.35f), 0.0f, 1.0f);
			data->label_alpha = std::max(data->label_alpha, 0.35f);
		}
	}

	data->prev_lmb = lmb;
	data->prev_rmb = rmb;

	for (int i = 0; i < kMaxClickRings; i++) {
		if (!data->rings[i].active)
			continue;
		data->rings[i].life -= seconds * 2.2f;
		if (data->rings[i].life <= 0.0f)
			data->rings[i].active = false;
	}

	const float speed_norm = std::clamp(data->curr_speed / 40.0f, 0.0f, 1.0f);
	float target_radius = clicking ? data->radius_click : data->radius_idle;
	target_radius *= (1.0f + speed_norm * 0.35f);
	const float visual_t = lerp_factor(0.35f, seconds);
	data->curr_radius += (target_radius - data->curr_radius) * visual_t;

	vec4 target_color;
	if (lmb)
		color_from_obs(data->color_lmb, &target_color);
	else if (rmb)
		color_from_obs(data->color_rmb, &target_color);
	else
		color_from_obs(data->color_idle, &target_color);
	target_color.x = std::min(1.0f, target_color.x + speed_norm * 0.15f);
	lerp_vec4(&data->curr_color, &target_color, visual_t);

	if (data->attracting)
		trail_clear(data);
	else if (showing && data->trail_length > 0)
		trail_push(data, data->curr_x, data->curr_y, data->curr_speed);
	else if (!showing)
		trail_clear(data);
}

static void sliding_video_render(void *data_ptr, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	auto *data = static_cast<MouseSlidingData *>(data_ptr);

	if (!obs_source_showing(data->source))
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!solid)
		return;

	gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
	const float half_x = canvas_half_x(data);
	const float half_y = canvas_half_y(data);
	const float size_w = data->canvas_w;
	const float size_h = data->canvas_h;
	const float gox = -data->edge_ox;
	const float goy = -data->edge_oy;

	gs_blend_state_push();
	gs_reset_blend_state();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	vec4 guide_color;
	vec4_set(&guide_color, 1.0f, 1.0f, 1.0f, 0.18f);

	if (data->show_pad_frame || data->show_axes || data->show_grid) {
		gs_effect_set_vec4(color_param, &guide_color);
		while (gs_effect_loop(solid, "Solid")) {
			if (data->show_pad_frame)
				draw_rect_outline(2.0f + gox, 2.0f + goy, size_w - 2.0f + gox, size_h - 2.0f + goy, 2.0f);
			if (data->show_axes) {
				draw_line_segment(gox, half_y + goy, size_w + gox, half_y + goy, 1.5f);
				draw_line_segment(half_x + gox, goy, half_x + gox, size_h + goy, 1.5f);
			}
			if (data->show_grid) {
				for (int i = 1; i < 4; i++) {
					const float px = size_w * (float)i / 4.0f;
					const float py = size_h * (float)i / 4.0f;
					draw_line_segment(px + gox, goy, px + gox, size_h + goy, 1.0f);
					draw_line_segment(gox, py + goy, size_w + gox, py + goy, 1.0f);
				}
			}
		}
	}

	if (data->edge_glow > 0.01f && data->edge_intensity > 0.02f) {
		vec4 edge_col;
		color_from_obs(data->color_trail_fast, &edge_col);
		edge_col.w = data->edge_intensity * data->edge_glow * 0.75f;
		const float thick = 3.0f + data->edge_intensity * 5.0f;
		gs_effect_set_vec4(color_param, &edge_col);
		while (gs_effect_loop(solid, "Solid")) {
			if (data->edge_hit_l)
				draw_line_segment(3.0f, 4.0f, 3.0f, size_h - 4.0f, thick);
			if (data->edge_hit_r)
				draw_line_segment(size_w - 3.0f, 4.0f, size_w - 3.0f, size_h - 4.0f, thick);
			if (data->edge_hit_t)
				draw_line_segment(4.0f, 3.0f, size_w - 4.0f, 3.0f, thick);
			if (data->edge_hit_b)
				draw_line_segment(4.0f, size_h - 3.0f, size_w - 4.0f, size_h - 3.0f, thick);
		}
	}

	vec4 trail_slow, trail_fast;
	color_from_obs(data->color_trail_slow, &trail_slow);
	color_from_obs(data->color_trail_fast, &trail_fast);
	draw_ribbon_batched(data, data->trail_width, 35.0f, &trail_slow, &trail_fast, solid);

	bool any_ring = false;
	for (int i = 0; i < kMaxClickRings; i++) {
		if (data->rings[i].active) {
			any_ring = true;
			break;
		}
	}
	if (any_ring) {
		while (gs_effect_loop(solid, "Solid")) {
			for (int i = 0; i < kMaxClickRings; i++) {
				if (!data->rings[i].active)
					continue;
				const float life = std::clamp(data->rings[i].life, 0.0f, 1.0f);
				vec4 ring_col = data->rings[i].color;
				ring_col.w *= life * 0.85f;
				gs_effect_set_vec4(color_param, &ring_col);
				draw_circle_outline(data->rings[i].x, data->rings[i].y,
						    data->curr_radius + (1.0f - life) * 48.0f, 3.0f + life * 2.0f);
			}
		}
	}

	while (gs_effect_loop(solid, "Solid")) {
		if (data->glow_strength > 0.01f) {
			vec4 glow = data->curr_color;
			glow.w *= data->glow_strength * 0.28f;
			gs_effect_set_vec4(color_param, &glow);
			draw_filled_circle(data->curr_x, data->curr_y, data->curr_radius * 1.85f);
		}
		gs_effect_set_vec4(color_param, &data->curr_color);
		draw_filled_circle(data->curr_x, data->curr_y, data->curr_radius);
	}

	if (data->show_wheel && data->wheel_alpha > 0.02f) {
		vec4 idle_c, fast_c;
		color_from_obs(data->color_idle, &idle_c);
		color_from_obs(data->color_trail_fast, &fast_c);
		const float burst = std::clamp(fabsf(data->wheel_omega) / 8.0f, 0.0f, 1.0f);
		vec4 arc_col;
		arc_col.x = idle_c.x + (fast_c.x - idle_c.x) * burst;
		arc_col.y = idle_c.y + (fast_c.y - idle_c.y) * burst;
		arc_col.z = idle_c.z + (fast_c.z - idle_c.z) * burst;
		arc_col.w = 1.0f;

		const float pop = 1.0f + data->wheel_pop * 0.12f;
		const float radius = data->curr_radius * 1.55f * data->wheel_scale * pop;
		const float thickness = (2.2f + burst * 1.4f) * data->wheel_scale;
		const float span = (float)M_PI * (120.0f / 180.0f);
		const float nose = data->wheel_angle;
		const float a0 = nose - span * 0.5f;
		const float a1 = nose + span * 0.5f;

		while (gs_effect_loop(solid, "Solid")) {
			for (int g = 2; g >= 1; g--) {
				vec4 ghost = arc_col;
				ghost.w = data->wheel_alpha * (0.18f / (float)g);
				gs_effect_set_vec4(color_param, &ghost);
				const float lag = data->wheel_spin_sign * 0.22f * (float)g;
				draw_arc_outline(data->curr_x, data->curr_y, radius, thickness * 0.85f, a0 - lag,
						 a1 - lag, kWheelArcSegments);
			}
			const float third = span / 3.0f;
			for (int b = 0; b < 3; b++) {
				vec4 band = arc_col;
				band.w = data->wheel_alpha * (0.35f + 0.22f * (float)b);
				gs_effect_set_vec4(color_param, &band);
				draw_arc_outline(data->curr_x, data->curr_y, radius, thickness, a0 + third * (float)b,
						 a0 + third * (float)(b + 1), 4);
			}
		}
	}

	gs_blend_state_pop();
	draw_combo_label(data);
}

static obs_properties_t *sliding_properties(void *data_ptr)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_t *canvas = obs_properties_create();
	obs_properties_add_float_slider(canvas, "canvas_width", obs_module_text("CanvasWidth"), 128.0, 1920.0, 16.0);
	obs_properties_add_float_slider(canvas, "canvas_height", obs_module_text("CanvasHeight"), 128.0, 1080.0, 16.0);
	obs_properties_add_group(props, "group_canvas", obs_module_text("GroupCanvas"), OBS_GROUP_NORMAL, canvas);

	obs_properties_t *pad = obs_properties_create();
	obs_properties_add_float_slider(pad, "pad_sensitivity", obs_module_text("PadSensitivity"), 0.2, 4.0, 0.05);
	obs_properties_add_float_slider(pad, "pad_follow", obs_module_text("PadFollow"), 0.1, 1.0, 0.05);
	obs_properties_add_button2(pad, "pad_recenter", obs_module_text("PadRecenter"), sliding_recenter_clicked,
				  data_ptr);
	obs_properties_add_group(props, "group_pad", obs_module_text("GroupPad"), OBS_GROUP_NORMAL, pad);

	obs_properties_t *trail = obs_properties_create();
	obs_properties_add_int_slider(trail, "trail_length", obs_module_text("TrailLength"), 0, 64, 1);
	obs_properties_add_float_slider(trail, "trail_width", obs_module_text("TrailWidth"), 2.0, 40.0, 1.0);
	obs_properties_add_color(trail, "color_trail_slow", obs_module_text("ColorTrailSlow"));
	obs_properties_add_color(trail, "color_trail_fast", obs_module_text("ColorTrailFast"));
	obs_properties_add_group(props, "group_trail", obs_module_text("GroupTrail"), OBS_GROUP_NORMAL, trail);

	obs_properties_t *marker = obs_properties_create();
	obs_properties_add_color(marker, "color_idle", obs_module_text("ColorIdle"));
	obs_properties_add_color(marker, "color_lmb", obs_module_text("ColorLmb"));
	obs_properties_add_color(marker, "color_rmb", obs_module_text("ColorRmb"));
	obs_properties_add_float_slider(marker, "radius_idle", obs_module_text("RadiusIdle"), 5.0, 100.0, 1.0);
	obs_properties_add_float_slider(marker, "radius_click", obs_module_text("RadiusClick"), 5.0, 100.0, 1.0);
	obs_properties_add_float_slider(marker, "glow_strength", obs_module_text("GlowStrength"), 0.0, 1.0, 0.05);
	obs_properties_add_group(props, "group_marker", obs_module_text("GroupMarker"), OBS_GROUP_NORMAL, marker);

	obs_properties_t *guides = obs_properties_create();
	obs_properties_add_bool(guides, "show_pad_frame", obs_module_text("ShowPadFrame"));
	obs_properties_add_bool(guides, "show_axes", obs_module_text("ShowAxes"));
	obs_properties_add_bool(guides, "show_grid", obs_module_text("ShowGrid"));
	obs_properties_add_group(props, "group_guides", obs_module_text("GroupGuides"), OBS_GROUP_NORMAL, guides);

	obs_properties_t *edge_attract = obs_properties_create();
	obs_properties_add_float_slider(edge_attract, "edge_parallax", obs_module_text("EdgeParallax"), 0.0, 28.0,
					1.0);
	obs_properties_add_float_slider(edge_attract, "edge_glow", obs_module_text("EdgeGlow"), 0.0, 1.0, 0.05);
	obs_properties_add_bool(edge_attract, "attract_enabled", obs_module_text("AttractGroup"));
	obs_properties_add_bool(edge_attract, "attract_on_idle", obs_module_text("AttractIdle"));
	obs_properties_add_float_slider(edge_attract, "attract_idle_sec", obs_module_text("AttractIdleSec"), 0.2, 10.0,
					0.1);
	obs_properties_add_bool(edge_attract, "attract_on_edge", obs_module_text("AttractEdge"));
	obs_properties_add_float_slider(edge_attract, "attract_strength", obs_module_text("AttractStrength"), 0.05, 1.0,
					0.05);
	obs_properties_add_group(props, "group_edge_attract", obs_module_text("GroupEdgeAttract"), OBS_GROUP_NORMAL,
				 edge_attract);

	obs_properties_t *wheel = obs_properties_create();
	obs_properties_add_float_slider(wheel, "wheel_scale", obs_module_text("WheelScale"), 0.5, 2.0, 0.05);
	obs_properties_add_group(props, "show_wheel", obs_module_text("ShowWheel"), OBS_GROUP_CHECKABLE, wheel);

	obs_properties_t *combo = obs_properties_create();
	obs_property_t *font = obs_properties_add_list(combo, "combo_font", obs_module_text("ComboFont"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(font, obs_module_text("FontArcade"), "arcade");
	obs_property_list_add_string(font, obs_module_text("FontBlock"), "block");
	obs_property_list_add_string(font, obs_module_text("FontSlim"), "slim");
	obs_properties_add_text(combo, "combo_text_single", obs_module_text("ComboTextSingle"), OBS_TEXT_DEFAULT);
	obs_properties_add_text(combo, "combo_text_multi", obs_module_text("ComboTextMulti"), OBS_TEXT_DEFAULT);
	obs_properties_add_float_slider(combo, "combo_window", obs_module_text("ComboWindow"), 0.15, 1.0, 0.05);
	obs_properties_add_float_slider(combo, "hold_fire_rate", obs_module_text("HoldFireRate"), 2.0, 20.0, 0.5);
	obs_properties_add_float_slider(combo, "combo_scale", obs_module_text("ComboScale"), 0.2, 4.0, 0.05);
	obs_properties_add_float_slider(combo, "combo_offset_y", obs_module_text("ComboOffsetY"), 10.0, 120.0, 1.0);
	obs_properties_add_color(combo, "combo_color", obs_module_text("ComboColor"));
	obs_properties_add_color(combo, "combo_color_hot", obs_module_text("ComboColorHot"));
	obs_properties_add_int_slider(combo, "combo_hot_threshold", obs_module_text("ComboHotThreshold"), 2, 50, 1);
	obs_properties_add_group(props, "combo_enabled", obs_module_text("ComboEnabled"), OBS_GROUP_CHECKABLE, combo);

	char links_html[768];
	snprintf(links_html, sizeof(links_html),
		 "<p style=\"margin:6px 0; white-space:nowrap\">"
		 "<a href=\"%s\">%s</a>"
		 "&nbsp;&nbsp;&nbsp;|&nbsp;&nbsp;&nbsp;"
		 "<a href=\"%s\">%s</a>"
		 "</p>",
		 kSupportUrl, obs_module_text("OpenSupport"), kGitHubRepoUrl, obs_module_text("OpenGitHub"));
	obs_property_t *links = obs_properties_add_text(props, "author_links", links_html, OBS_TEXT_INFO);
	obs_property_text_set_info_type(links, OBS_TEXT_INFO_NORMAL);

	return props;
}

static void sliding_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "canvas_width", 400.0);
	obs_data_set_default_double(settings, "canvas_height", 400.0);
	obs_data_set_default_int(settings, "color_idle", 0xFFFFFF00);
	obs_data_set_default_int(settings, "color_lmb", 0xFFFF00FF);
	obs_data_set_default_int(settings, "color_rmb", 0xFF00AAFF);
	obs_data_set_default_int(settings, "color_trail_slow", 0xFFCCAA00);
	obs_data_set_default_int(settings, "color_trail_fast", 0xFFFF00FF);
	obs_data_set_default_int(settings, "combo_color", 0xFF00FFFF);
	obs_data_set_default_int(settings, "combo_color_hot", 0xFF0040FF);
	obs_data_set_default_int(settings, "combo_hot_threshold", 10);

	obs_data_set_default_double(settings, "radius_idle", 18.0);
	obs_data_set_default_double(settings, "radius_click", 14.0);
	obs_data_set_default_double(settings, "pad_sensitivity", 1.0);
	obs_data_set_default_double(settings, "pad_follow", 0.55);
	obs_data_set_default_double(settings, "glow_strength", 0.45);
	obs_data_set_default_double(settings, "trail_width", 14.0);
	obs_data_set_default_int(settings, "trail_length", 16);

	obs_data_set_default_bool(settings, "show_pad_frame", true);
	obs_data_set_default_bool(settings, "show_axes", true);
	obs_data_set_default_bool(settings, "show_grid", false);
	obs_data_set_default_double(settings, "edge_parallax", 12.0);
	obs_data_set_default_double(settings, "edge_glow", 0.55);
	obs_data_set_default_bool(settings, "show_wheel", true);
	obs_data_set_default_double(settings, "wheel_scale", 1.0);

	obs_data_set_default_bool(settings, "attract_enabled", false);
	obs_data_set_default_bool(settings, "attract_on_idle", true);
	obs_data_set_default_bool(settings, "attract_on_edge", false);
	obs_data_set_default_double(settings, "attract_idle_sec", 1.5);
	obs_data_set_default_double(settings, "attract_strength", 0.35);

	obs_data_set_default_bool(settings, "combo_enabled", true);
	obs_data_set_default_string(settings, "combo_font", "arcade");
	obs_data_set_default_string(settings, "combo_text_single", "HIT");
	obs_data_set_default_string(settings, "combo_text_multi", "{n} HIT COMBO");
	obs_data_set_default_double(settings, "combo_window", 0.40);
	obs_data_set_default_double(settings, "hold_fire_rate", 10.0);
	obs_data_set_default_double(settings, "combo_scale", 1.2);
	obs_data_set_default_double(settings, "combo_offset_y", 42.0);
}

static obs_source_info make_sliding_source_info()
{
	obs_source_info info = {};
	info.id = "mouse_sliding";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_SRGB;
	info.get_name = sliding_get_name;
	info.create = sliding_create;
	info.destroy = sliding_destroy;
	info.get_width = sliding_get_width;
	info.get_height = sliding_get_height;
	info.get_defaults = sliding_defaults;
	info.get_properties = sliding_properties;
	info.update = sliding_update;
	info.video_tick = sliding_video_tick;
	info.video_render = sliding_video_render;
	info.icon_type = OBS_ICON_TYPE_CUSTOM;
	return info;
}

struct obs_source_info mouse_sliding_source_info = make_sliding_source_info();
