const std = @import("std");
const builtin = @import("builtin");

const c = @cImport({
    @cInclude("config_font.h");
    @cInclude("han_table.h");
    @cInclude("raylib.h");
});

const default_font_path = "fonts/MapleMono-NF-CN-Regular.ttf";

fn trimAscii(s: []u8) []u8 {
    var start: usize = 0;
    while (start < s.len and (s[start] == ' ' or s[start] == '\t' or s[start] == '\r' or s[start] == '\n')) start += 1;
    if (start == s.len) return s[0..0];
    var end: usize = s.len;
    while (end > start and (s[end - 1] == ' ' or s[end - 1] == '\t' or s[end - 1] == '\r' or s[end - 1] == '\n')) end -= 1;
    return s[start..end];
}

fn fileIsReadable(path: []const u8) bool {
    const f = std.fs.cwd().openFile(path, .{}) catch return false;
    f.close();
    return true;
}

fn pathIsAbsolute(path: []const u8) bool {
    if (path.len == 0) return false;
    if (builtin.os.tag == .windows) {
        if (path.len >= 3 and std.ascii.isAlpha(path[0]) and path[1] == ':' and (path[2] == '\\' or path[2] == '/')) return true;
        if (path[0] == '\\' or path[0] == '/') return true;
        return false;
    }
    return path[0] == '/';
}

fn cwdBuf(buf: []u8) ?[]u8 {
    return std.posix.getcwd(buf) catch null;
}

fn tryRelativeCandidates(base_dir: []const u8, rel_path: []const u8, out: []u8) bool {
    const prefixes = [_][]const u8{ "", "..", "../..", "../../.." };
    for (prefixes) |prefix| {
        var candidate: [4096]u8 = undefined;
        const n = if (prefix.len > 0)
            @import("std").fmt.bufPrint(&candidate, "{s}/{s}/{s}", .{ base_dir, prefix, rel_path }) catch continue
        else
            @import("std").fmt.bufPrint(&candidate, "{s}/{s}", .{ base_dir, rel_path }) catch continue;
        if (fileIsReadable(candidate[0..n.len])) {
            const len = @min(n.len, out.len - 1);
            @memcpy(out[0..len], n[0..len]);
            out[len] = 0;
            return true;
        }
    }
    return false;
}

fn resolveReadableFontPath(path: []const u8, out: []u8) bool {
    if (path.len == 0) return false;
    if (fileIsReadable(path)) {
        const len = @min(path.len, out.len - 1);
        @memcpy(out[0..len], path[0..len]);
        out[len] = 0;
        return true;
    }
    if (pathIsAbsolute(path)) return false;
    var cwdbuf: [4096]u8 = undefined;
    const cwd = cwdBuf(&cwdbuf) orelse return false;
    return tryRelativeCandidates(cwd, path, out);
}

fn configHomePath(out: []u8) bool {
    if (builtin.os.tag == .windows) {
        const app = std.posix.getenv("APPDATA") orelse return false;
        const n = std.fmt.bufPrint(out, "{s}\\ghostling\\config", .{app}) catch return false;
        _ = n;
        return true;
    }
    const home = std.posix.getenv("HOME") orelse return false;
    const n = std.fmt.bufPrint(out, "{s}/.config/ghostling/config", .{home}) catch return false;
    _ = n;
    return true;
}

fn configLocalPath(out: []u8) bool {
    var cwdbuf: [4096]u8 = undefined;
    const cwd = cwdBuf(&cwdbuf) orelse return false;
    const n1 = std.fmt.bufPrint(out, "{s}/config", .{cwd}) catch return false;
    if (fileIsReadable(out[0..n1.len])) return true;
    const n2 = std.fmt.bufPrint(out, "{s}/config.example", .{cwd}) catch return false;
    if (fileIsReadable(out[0..n2.len])) return true;
    return false;
}

fn asciiLowerCopy(out: []u8, in_str: []const u8) void {
    var i: usize = 0;
    while (i < out.len - 1 and i < in_str.len) : (i += 1) {
        out[i] = std.ascii.toLower(in_str[i]);
    }
    out[i] = 0;
}

fn codepointSetValid(set_name: []const u8) bool {
    return std.mem.eql(u8, set_name, "full") or
        std.mem.eql(u8, set_name, "compact") or
        std.mem.eql(u8, set_name, "latin") or
        std.mem.eql(u8, set_name, "han3500") or
        std.mem.eql(u8, set_name, "han6500") or
        std.mem.eql(u8, set_name, "han8105");
}

fn hanTableContains(table: [*]const u32, count: usize, cp: u32) bool {
    var lo: usize = 0;
    var hi = count;
    while (lo < hi) {
        const mid = lo + (hi - lo) / 2;
        const v = table[mid];
        if (v == cp) return true;
        if (v < cp) lo = mid + 1 else hi = mid;
    }
    return false;
}

fn parseKeyName(name: []const u8) c_int {
    const rl = struct {
        const KEY_APOSTROPHE = 39;
        const KEY_COMMA = 44;
        const KEY_MINUS = 45;
        const KEY_PERIOD = 46;
        const KEY_SLASH = 47;
        const KEY_ZERO = 48;
        const KEY_NINE = 57;
        const KEY_SEMICOLON = 59;
        const KEY_EQUAL = 61;
        const KEY_LEFT_BRACKET = 91;
        const KEY_BACKSLASH = 92;
        const KEY_RIGHT_BRACKET = 93;
        const KEY_GRAVE = 96;
        const KEY_SPACE = 32;
        const KEY_ENTER = 257;
        const KEY_TAB = 258;
        const KEY_BACKSPACE = 259;
        const KEY_INSERT = 260;
        const KEY_DELETE = 261;
        const KEY_RIGHT = 262;
        const KEY_LEFT = 263;
        const KEY_DOWN = 264;
        const KEY_UP = 265;
        const KEY_PAGE_UP = 266;
        const KEY_PAGE_DOWN = 267;
        const KEY_HOME = 268;
        const KEY_END = 269;
        const KEY_CAPS_LOCK = 280;
        const KEY_SCROLL_LOCK = 281;
        const KEY_NUM_LOCK = 282;
        const KEY_PRINT_SCREEN = 283;
        const KEY_PAUSE = 284;
        const KEY_F1 = 290;
        const KEY_F12 = 301;
        const KEY_LEFT_SHIFT = 340;
        const KEY_LEFT_CONTROL = 341;
        const KEY_LEFT_ALT = 342;
        const KEY_LEFT_SUPER = 343;
        const KEY_RIGHT_SHIFT = 344;
        const KEY_RIGHT_CONTROL = 345;
        const KEY_RIGHT_ALT = 346;
        const KEY_RIGHT_SUPER = 347;
        const KEY_KB_MENU = 348;
        const KEY_KP_0 = 320;
        const KEY_KP_9 = 329;
        const KEY_KP_DECIMAL = 330;
        const KEY_KP_DIVIDE = 331;
        const KEY_KP_MULTIPLY = 332;
        const KEY_KP_SUBTRACT = 333;
        const KEY_KP_ADD = 334;
        const KEY_KP_ENTER = 335;
        const KEY_KP_EQUAL = 336;
        const KEY_BACK = 4;
    };

    if (name.len == 1) return @as(c_int, name[0]);
    var lower_buf: [128]u8 = undefined;
    const lower = std.ascii.lowerString(&lower_buf, name);

    inline for (.{
        .{ "space", rl.KEY_SPACE },
        .{ "enter", rl.KEY_ENTER },
        .{ "return", rl.KEY_ENTER },
        .{ "tab", rl.KEY_TAB },
        .{ "backspace", rl.KEY_BACKSPACE },
        .{ "insert", rl.KEY_INSERT },
        .{ "delete", rl.KEY_DELETE },
        .{ "del", rl.KEY_DELETE },
        .{ "right", rl.KEY_RIGHT },
        .{ "left", rl.KEY_LEFT },
        .{ "down", rl.KEY_DOWN },
        .{ "up", rl.KEY_UP },
        .{ "pageup", rl.KEY_PAGE_UP },
        .{ "pagedown", rl.KEY_PAGE_DOWN },
        .{ "home", rl.KEY_HOME },
        .{ "end", rl.KEY_END },
        .{ "capslock", rl.KEY_CAPS_LOCK },
        .{ "scrolllock", rl.KEY_SCROLL_LOCK },
        .{ "numlock", rl.KEY_NUM_LOCK },
        .{ "printscreen", rl.KEY_PRINT_SCREEN },
        .{ "pause", rl.KEY_PAUSE },
        .{ "menu", rl.KEY_KB_MENU },
        .{ "leftshift", rl.KEY_LEFT_SHIFT },
        .{ "leftcontrol", rl.KEY_LEFT_CONTROL },
        .{ "leftalt", rl.KEY_LEFT_ALT },
        .{ "leftsuper", rl.KEY_LEFT_SUPER },
        .{ "rightshift", rl.KEY_RIGHT_SHIFT },
        .{ "rightcontrol", rl.KEY_RIGHT_CONTROL },
        .{ "rightalt", rl.KEY_RIGHT_ALT },
        .{ "rightsuper", rl.KEY_RIGHT_SUPER },
    }) |entry| {
        if (std.mem.eql(u8, lower, @as([]const u8, entry[0]))) return entry[1];
    }

    if (name.len == 2 and name[0] == 'f') {
        const d = std.fmt.parseInt(c_int, name[1..], 10) catch 0;
        if (d >= 1 and d <= 12) return rl.KEY_F1 + (d - 1);
    }

    return c.KEY_NULL;
}

fn parseKeybinding(token: []const u8, out: *c.GhostlingKeyBinding) void {
    out.* = c.GhostlingKeyBinding{ .key = c.KEY_NULL, .mods = 0, .enabled = false };
    if (std.mem.eql(u8, token, "none") or std.mem.eql(u8, token, "off") or std.mem.eql(u8, token, "disabled")) return;

    var mods: u8 = 0;
    var key_start: usize = 0;

    var it = std.mem.splitScalar(u8, token, '+');
    while (it.next()) |part| {
        var lower2_buf: [128]u8 = undefined;
        const lower = std.ascii.lowerString(&lower2_buf, part);
        if (std.mem.eql(u8, lower, "primary")) {
            mods |= @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY));
        } else if (std.mem.eql(u8, lower, "ctrl") or std.mem.eql(u8, lower, "control")) {
            mods |= @as(u8, @intCast(c.GHOSTLING_KEYMOD_CTRL));
        } else if (std.mem.eql(u8, lower, "alt") or std.mem.eql(u8, lower, "option")) {
            mods |= @as(u8, @intCast(c.GHOSTLING_KEYMOD_ALT));
        } else if (std.mem.eql(u8, lower, "shift")) {
            mods |= @as(u8, @intCast(c.GHOSTLING_KEYMOD_SHIFT));
        } else if (std.mem.eql(u8, lower, "super") or std.mem.eql(u8, lower, "cmd") or std.mem.eql(u8, lower, "command") or std.mem.eql(u8, lower, "win") or std.mem.eql(u8, lower, "windows")) {
            mods |= @as(u8, @intCast(c.GHOSTLING_KEYMOD_SUPER));
        } else {
            key_start = @intFromPtr(part.ptr) - @intFromPtr(token.ptr);
        }
    }

    const key_name = std.mem.trim(u8, token[key_start..], " ");
    const key = parseKeyName(key_name);
    out.enabled = true;
    out.mods = mods;
    out.key = key;
}

fn parseProfileOverrideKey(full_key: []const u8, profile_name: []u8, real_key: []u8) bool {
    const prefix = "profile.";
    if (full_key.len < prefix.len or !std.mem.startsWith(u8, full_key, prefix)) return false;
    const rest = full_key[prefix.len..];
    const dot = std.mem.indexOfScalar(u8, rest, '.') orelse return false;
    const pname = rest[0..dot];
    const rkey = rest[dot + 1 ..];
    const n1 = @min(pname.len, profile_name.len - 1);
    @memcpy(profile_name[0..n1], pname[0..n1]);
    profile_name[n1] = 0;
    const n2 = @min(rkey.len, real_key.len - 1);
    @memcpy(real_key[0..n2], rkey[0..n2]);
    real_key[n2] = 0;
    return true;
}

fn configSetDefault(cfg: *c.AppConfig) void {
    cfg.* = std.mem.zeroes(c.AppConfig);
    cfg.font_size = 16;
    _ = std.mem.copyForwards(u8, cfg.font_path[0..default_font_path.len], default_font_path);
    cfg.font_path[default_font_path.len] = 0;
    _ = std.mem.copyForwards(u8, &cfg.font_codepoint_set, "full");
    cfg.tab_title_font_scale = 1.0;
    cfg.tab_title_h = 18;
    cfg.tab_reserved_h = 24;
    cfg.selection_copy_on_select = true;
    cfg.selection_copy_shortcut = c.GHOSTLING_COPY_SHORTCUT_CTRL_C;
    cfg.paste_shortcut = c.GHOSTLING_PASTE_SHORTCUT_CTRL_SHIFT_V;
    if (builtin.os.tag == .macos) {
        cfg.paste_shortcut = c.GHOSTLING_PASTE_SHORTCUT_CTRL_V;
    }
    cfg.key_new_tab = c.GhostlingKeyBinding{ .key = c.KEY_TAB, .mods = @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY)), .enabled = true };
    cfg.key_close_tab = c.GhostlingKeyBinding{ .key = c.KEY_W, .mods = @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY)), .enabled = true };
    cfg.key_next_tab = c.GhostlingKeyBinding{ .key = c.KEY_TAB, .mods = @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY | c.GHOSTLING_KEYMOD_SHIFT)), .enabled = false };
    cfg.key_prev_tab = c.GhostlingKeyBinding{ .key = c.KEY_TAB, .mods = @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY | c.GHOSTLING_KEYMOD_SHIFT)), .enabled = false };
    cfg.key_toggle_tab_strip = c.GhostlingKeyBinding{ .key = c.KEY_B, .mods = @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY)), .enabled = true };
    cfg.key_reload_config = c.GhostlingKeyBinding{ .key = c.KEY_R, .mods = @as(u8, @intCast(c.GHOSTLING_KEYMOD_PRIMARY)), .enabled = true };
}

fn applyConfigLine(cfg: *c.AppConfig, line_raw: []const u8, profile: []const u8) void {
    const trimmed = std.mem.trim(u8, line_raw, " \t\r\n");
    if (trimmed.len == 0 or trimmed[0] == '#') return;

    const eq = std.mem.indexOfScalar(u8, trimmed, '=') orelse return;
    const key_raw = std.mem.trim(u8, trimmed[0..eq], " \t");
    const val_raw = std.mem.trim(u8, trimmed[eq + 1 ..], " \t");

    var key_lower_buf: [128]u8 = undefined;
    var key_buf: [128]u8 = undefined;
    const key_n = @min(key_raw.len, key_lower_buf.len - 1);
    for (key_raw[0..key_n], 0..) |ch, j| key_lower_buf[j] = std.ascii.toLower(ch);
    key_lower_buf[key_n] = 0;
    const key = key_lower_buf[0..key_n];

    var pname_buf: [64]u8 = undefined;
    var real_key_buf: [128]u8 = undefined;
    if (parseProfileOverrideKey(key, &pname_buf, &real_key_buf)) {
        const pname = std.mem.sliceTo(&pname_buf, 0);
        const rkey = std.mem.sliceTo(&real_key_buf, 0);
        if (!std.mem.eql(u8, pname, profile)) return;
        const key_n2 = @min(rkey.len, key_buf.len - 1);
        for (rkey[0..key_n2], 0..) |ch, j| key_buf[j] = std.ascii.toLower(ch);
        key_buf[key_n2] = 0;
        applyConfigField(cfg, std.mem.sliceTo(&key_buf, 0), val_raw);
    } else {
        if (std.mem.eql(u8, key, "profile")) {
            const n = @min(val_raw.len, cfg.profile.len - 1);
            @memcpy(cfg.profile[0..n], val_raw[0..n]);
            cfg.profile[n] = 0;
        } else {
            applyConfigField(cfg, key, val_raw);
        }
    }
}

fn applyConfigField(cfg: *c.AppConfig, key: []const u8, val: []const u8) void {
    if (std.mem.eql(u8, key, "font_path")) {
        const n = @min(val.len, cfg.font_path.len - 1);
        @memcpy(cfg.font_path[0..n], val[0..n]);
        cfg.font_path[n] = 0;
        cfg.font_path_from_config = true;
    } else if (std.mem.eql(u8, key, "font_size")) {
        cfg.font_size = std.fmt.parseInt(i32, val, 10) catch return;
    } else if (std.mem.eql(u8, key, "font_codepoint_set")) {
        if (codepointSetValid(val)) {
            const n = @min(val.len, cfg.font_codepoint_set.len - 1);
            @memcpy(cfg.font_codepoint_set[0..n], val[0..n]);
            cfg.font_codepoint_set[n] = 0;
        }
    } else if (std.mem.eql(u8, key, "tab_title_font_scale")) {
        cfg.tab_title_font_scale = std.fmt.parseFloat(f32, val) catch return;
    } else if (std.mem.eql(u8, key, "tab_title_h")) {
        cfg.tab_title_h = std.fmt.parseInt(i32, val, 10) catch return;
    } else if (std.mem.eql(u8, key, "tab_reserved_h")) {
        cfg.tab_reserved_h = std.fmt.parseInt(i32, val, 10) catch return;
    } else if (std.mem.eql(u8, key, "selection_copy_on_select")) {
        cfg.selection_copy_on_select = std.mem.eql(u8, val, "true") or std.mem.eql(u8, val, "1");
    } else if (std.mem.eql(u8, key, "selection_copy_shortcut")) {
        if (std.mem.eql(u8, val, "ctrl+shift+c")) cfg.selection_copy_shortcut = c.GHOSTLING_COPY_SHORTCUT_CTRL_SHIFT_C;
    } else if (std.mem.eql(u8, key, "paste_shortcut")) {
        if (std.mem.eql(u8, val, "ctrl+v")) cfg.paste_shortcut = c.GHOSTLING_PASTE_SHORTCUT_CTRL_V;
        if (std.mem.eql(u8, val, "none")) cfg.paste_shortcut = c.GHOSTLING_PASTE_SHORTCUT_NONE;
    } else if (std.mem.eql(u8, key, "key_new_tab")) {
        parseKeybinding(val, &cfg.key_new_tab);
    } else if (std.mem.eql(u8, key, "key_close_tab")) {
        parseKeybinding(val, &cfg.key_close_tab);
    } else if (std.mem.eql(u8, key, "key_next_tab")) {
        parseKeybinding(val, &cfg.key_next_tab);
    } else if (std.mem.eql(u8, key, "key_prev_tab")) {
        parseKeybinding(val, &cfg.key_prev_tab);
    } else if (std.mem.eql(u8, key, "key_toggle_tab_strip")) {
        parseKeybinding(val, &cfg.key_toggle_tab_strip);
    } else if (std.mem.eql(u8, key, "key_reload_config")) {
        parseKeybinding(val, &cfg.key_reload_config);
    }
}

export fn config_resolve_path(profile: [*c]const u8, out: [*c]u8, out_sz: usize) bool {
    _ = profile;
    const out_slice = out[0..out_sz];
    if (configLocalPath(out_slice)) return true;
    if (configHomePath(out_slice) and fileIsReadable(std.mem.sliceTo(out, 0))) return true;
    if (out_sz > 0) out[0] = 0;
    return false;
}

export fn config_load(cfg: *c.AppConfig) void {
    configSetDefault(cfg);

    var path_buf: [4096]u8 = undefined;
    if (!configLocalPath(&path_buf) and !configHomePath(&path_buf)) return;
    if (!fileIsReadable(std.mem.sliceTo(&path_buf, 0))) return;

    const path_n = std.mem.indexOfScalar(u8, &path_buf, 0) orelse path_buf.len;
    const path = path_buf[0..path_n];
    const n = @min(path.len, cfg.loaded_config_path.len - 1);
    @memcpy(cfg.loaded_config_path[0..n], path[0..n]);
    cfg.loaded_config_path[n] = 0;

    const file = std.fs.cwd().openFile(path, .{}) catch return;
    defer file.close();

    const profile = std.mem.sliceTo(&cfg.profile, 0);

    var content: [32768]u8 = undefined;
    const bytes_read = file.read(&content) catch return;
    var line_start: usize = 0;
    var i: usize = 0;
    while (i < bytes_read) : (i += 1) {
        if (content[i] == '\n') {
            applyConfigLine(cfg, content[line_start..i], profile);
            line_start = i + 1;
        }
    }
    if (line_start < bytes_read) {
        applyConfigLine(cfg, content[line_start..bytes_read], profile);
    }
}

export fn config_load_profile(cfg: *c.AppConfig, profile: [*c]const u8) void {
    configSetDefault(cfg);
    const profile_slice = std.mem.sliceTo(profile, 0);
    const n = @min(profile_slice.len, cfg.profile.len - 1);
    @memcpy(cfg.profile[0..n], profile_slice[0..n]);
    cfg.profile[n] = 0;

    var path_buf: [4096]u8 = undefined;
    if (!configLocalPath(&path_buf) and !configHomePath(&path_buf)) return;
    if (!fileIsReadable(std.mem.sliceTo(&path_buf, 0))) return;

    const path_n = std.mem.indexOfScalar(u8, &path_buf, 0) orelse path_buf.len;
    const file = std.fs.cwd().openFile(path_buf[0..path_n], .{}) catch return;
    defer file.close();

    var content: [32768]u8 = undefined;
    const bytes_read = file.read(&content) catch return;
    var line_start: usize = 0;
    var i: usize = 0;
    while (i < bytes_read) : (i += 1) {
        if (content[i] == '\n') {
            applyConfigLine(cfg, content[line_start..i], profile_slice);
            line_start = i + 1;
        }
    }
    if (line_start < bytes_read) {
        applyConfigLine(cfg, content[line_start..bytes_read], profile_slice);
    }
}

export fn build_terminal_codepoints(set_name: [*c]const u8, out_count: *c_int) [*c]c_int {
    const name = std.mem.sliceTo(set_name, 0);

    const cap: c_int = 65536;
    const alloc = std.heap.c_allocator;
    const cp_list = alloc.alloc(c_int, @as(usize, @intCast(cap))) catch {
        out_count.* = 0;
        return null;
    };

    var idx: usize = 0;

    const Range = struct {
        fn append(list: []c_int, n: *usize, lo: c_int, hi: c_int) bool {
            var cp = lo;
            while (cp <= hi) : (cp += 1) {
                if (n.* >= cap) return false;
                list[n.*] = cp;
                n.* += 1;
            }
            return true;
        }
    };

    const appendCodepoints = struct {
        fn call(list: []c_int, n: *usize, values: [*c]const u32, count: usize) bool {
            var i: usize = 0;
            while (i < count) : (i += 1) {
                if (n.* >= cap) return false;
                list[n.*] = @intCast(values[i]);
                n.* += 1;
            }
            return true;
        }
    }.call;

    // Base set shared by all profiles: Latin + symbols + box drawing.
    _ = Range.append(cp_list, &idx, 0x20, 0x7E);
    _ = Range.append(cp_list, &idx, 0xA0, 0x024F);
    _ = Range.append(cp_list, &idx, 0x0300, 0x036F);
    _ = Range.append(cp_list, &idx, 0x2000, 0x206F);
    _ = Range.append(cp_list, &idx, 0x20A0, 0x20CF);
    _ = Range.append(cp_list, &idx, 0x2100, 0x214F);
    _ = Range.append(cp_list, &idx, 0x2190, 0x21FF);
    _ = Range.append(cp_list, &idx, 0x2200, 0x22FF);
    _ = Range.append(cp_list, &idx, 0x2300, 0x23FF);
    _ = Range.append(cp_list, &idx, 0x2460, 0x24FF);
    _ = Range.append(cp_list, &idx, 0x2500, 0x257F);
    _ = Range.append(cp_list, &idx, 0x2580, 0x259F);
    _ = Range.append(cp_list, &idx, 0x25A0, 0x25FF);
    _ = Range.append(cp_list, &idx, 0x2600, 0x26FF);
    _ = Range.append(cp_list, &idx, 0x2700, 0x27BF);
    _ = Range.append(cp_list, &idx, 0xE0A0, 0xE0D7);

    const han_tier = ghostling_han_tier_from_codepoint_set(set_name);

    if (han_tier != c.GHOSTLING_HAN_TIER_NONE) {
        _ = Range.append(cp_list, &idx, 0x3000, 0x303F);
        _ = Range.append(cp_list, &idx, 0xFF00, 0xFFEF);
        _ = appendCodepoints(cp_list, &idx, @ptrCast(&c.ghostling_han_level1), c.ghostling_han_level1_count);
        if (han_tier >= c.GHOSTLING_HAN_TIER_6500)
            _ = appendCodepoints(cp_list, &idx, @ptrCast(&c.ghostling_han_level2), c.ghostling_han_level2_count);
        if (han_tier >= c.GHOSTLING_HAN_TIER_8105)
            _ = appendCodepoints(cp_list, &idx, @ptrCast(&c.ghostling_han_level3), c.ghostling_han_level3_count);
    } else if (!std.mem.eql(u8, name, "latin")) {
        _ = Range.append(cp_list, &idx, 0x3000, 0x30FF);
        _ = Range.append(cp_list, &idx, 0x31F0, 0x31FF);
        _ = Range.append(cp_list, &idx, 0x4E00, 0x9FFF);
        _ = Range.append(cp_list, &idx, 0xFF00, 0xFFEF);
    }

    if (!std.mem.eql(u8, name, "latin")) {
        _ = Range.append(cp_list, &idx, 0xE000, 0xE00A);
        _ = Range.append(cp_list, &idx, 0xE200, 0xE2A9);
        _ = Range.append(cp_list, &idx, 0xE300, 0xE3E3);
        _ = Range.append(cp_list, &idx, 0xE5FA, 0xE6B8);
        _ = Range.append(cp_list, &idx, 0xE700, 0xE8EF);
        _ = Range.append(cp_list, &idx, 0xEA60, 0xEC1E);
        _ = Range.append(cp_list, &idx, 0xED00, 0xEFCE);
        _ = Range.append(cp_list, &idx, 0xF000, 0xF533);
    }

    if (std.mem.eql(u8, name, "full")) {
        _ = Range.append(cp_list, &idx, 0x3200, 0x32FF);
        _ = Range.append(cp_list, &idx, 0x3300, 0x33FF);
        _ = Range.append(cp_list, &idx, 0x3400, 0x4DBF);
        _ = Range.append(cp_list, &idx, 0xFE00, 0xFE0F);
        _ = Range.append(cp_list, &idx, 0xFE10, 0xFE1F);
        _ = Range.append(cp_list, &idx, 0xFE30, 0xFE4F);
        _ = Range.append(cp_list, &idx, 0xF0001, 0xF1AF0);
    }

    out_count.* = @as(c_int, @intCast(idx));
    return cp_list.ptr;
}

export fn ghostling_han_tier_from_codepoint_set(set_name: [*c]const u8) c_int {
    const name = std.mem.sliceTo(set_name, 0);
    if (std.mem.eql(u8, name, "han3500")) return c.GHOSTLING_HAN_TIER_3500;
    if (std.mem.eql(u8, name, "han6500")) return c.GHOSTLING_HAN_TIER_6500;
    if (std.mem.eql(u8, name, "han8105")) return c.GHOSTLING_HAN_TIER_8105;
    if (std.mem.eql(u8, name, "full")) return c.GHOSTLING_HAN_TIER_8105;
    return c.GHOSTLING_HAN_TIER_NONE;
}

export fn ghostling_han_tier_for_codepoint(codepoint: u32) c_int {
    if (hanTableContains(&c.ghostling_han_level1, c.ghostling_han_level1_count, codepoint)) return c.GHOSTLING_HAN_TIER_3500;
    if (hanTableContains(&c.ghostling_han_level2, c.ghostling_han_level2_count, codepoint)) return c.GHOSTLING_HAN_TIER_6500;
    if (hanTableContains(&c.ghostling_han_level3, c.ghostling_han_level3_count, codepoint)) return c.GHOSTLING_HAN_TIER_8105;
    return c.GHOSTLING_HAN_TIER_NONE;
}

export fn ghostling_codepoint_set_for_han_tier(tier: c_int) [*c]const u8 {
    return switch (tier) {
        c.GHOSTLING_HAN_TIER_3500 => @as([*c]const u8, @ptrCast("han3500")),
        c.GHOSTLING_HAN_TIER_6500 => @as([*c]const u8, @ptrCast("han6500")),
        c.GHOSTLING_HAN_TIER_8105 => @as([*c]const u8, @ptrCast("han8105")),
        else => @as([*c]const u8, @ptrCast("")),
    };
}

export fn load_terminal_font(
    path: [*c]const u8,
    embed: [*c]const u8,
    embed_size: c_int,
    font_size_px: c_int,
    codepoints: [*c]c_int,
    cp_count: c_int,
) c.Font {
    const path_slice = std.mem.sliceTo(path, 0);
    var resolved_path: [4096]u8 = undefined;

    if (path_slice.len > 0 and resolveReadableFontPath(path_slice, &resolved_path)) {
        const resolved = std.mem.sliceTo(&resolved_path, 0);
        const f = c.LoadFontEx(@as([*c]const u8, @ptrCast(resolved.ptr)), font_size_px, codepoints, cp_count);
        if (f.glyphCount > 0 and f.texture.id > 0) {
            c.TraceLog(c.LOG_INFO, "ghostling: using font \"%s\"", @as([*c]const u8, @ptrCast(resolved.ptr)));
            return f;
        }
        c.TraceLog(c.LOG_WARNING, "ghostling: LoadFontEx failed for \"%s\", using embedded font", @as([*c]const u8, @ptrCast(resolved.ptr)));
    } else if (path_slice.len > 0) {
        c.TraceLog(c.LOG_WARNING, "ghostling: font not readable \"%s\", using embedded font", path);
    }

    c.TraceLog(c.LOG_INFO, "ghostling: using embedded fallback font");
    return c.LoadFontFromMemory(".ttf", @as([*c]const u8, @ptrCast(embed)), embed_size, font_size_px, codepoints, cp_count);
}
