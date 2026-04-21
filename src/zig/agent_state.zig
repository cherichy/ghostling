const std = @import("std");

const c = @cImport({
    @cInclude("agent_state.h");
});

fn monotonicMs() u64 {
    return @as(u64, @intCast(std.time.milliTimestamp()));
}

fn sourcePriority(source: c_int) u8 {
    return switch (source) {
        c.GHOSTLING_AGENT_SOURCE_PROCESS => 50,
        c.GHOSTLING_AGENT_SOURCE_PROTOCOL => 40,
        c.GHOSTLING_AGENT_SOURCE_LOCAL_INPUT => 30,
        c.GHOSTLING_AGENT_SOURCE_HEURISTIC => 10,
        else => 0,
    };
}

fn shouldOverride(s: *const c.GhostlingAgentState, nextValue: c_int, nextSource: c_int) bool {
    if (nextSource == c.GHOSTLING_AGENT_SOURCE_LOCAL_INPUT and
        nextValue == c.GHOSTLING_AGENT_STATE_RUNNING and
        s.source == c.GHOSTLING_AGENT_SOURCE_PROTOCOL and
        s.value != c.GHOSTLING_AGENT_STATE_RUNNING) return true;
    return sourcePriority(nextSource) >= sourcePriority(@as(c_int, @intCast(s.source)));
}

fn updateState(s: *c.GhostlingAgentState, value: anytype, source: anytype, confidence: u8) void {
    const v: c_int = @intCast(value);
    const src: c_int = @intCast(source);
    if (!shouldOverride(s, v, src)) return;
    if (s.value == value and s.source == source and s.confidence == confidence) return;
    s.value = @as(c_uint, @intCast(value));
    s.source = @as(c_uint, @intCast(source));
    s.confidence = confidence;
    s.updated_at_ms = monotonicMs();
}

fn appendTail(s: *c.GhostlingAgentState, data: []const u8) void {
    if (data.len == 0) return;
    const cap = s.tail.len - 1;
    if (data.len >= cap) {
        const keep = cap;
        @memcpy(s.tail[0..keep], data[data.len - keep ..]);
        s.tail[keep] = 0;
        s.tail_len = keep;
        return;
    }
    if (s.tail_len + data.len > cap) {
        const overflow = s.tail_len + data.len - cap;
        std.mem.copyForwards(u8, s.tail[0 .. s.tail_len - overflow], s.tail[overflow..s.tail_len]);
        s.tail_len -= overflow;
    }
    @memcpy(s.tail[s.tail_len .. s.tail_len + data.len], data);
    s.tail_len += data.len;
    s.tail[s.tail_len] = 0;
}

fn parseStateToken(token: []const u8) c_int {
    if (std.mem.eql(u8, token, "running")) return c.GHOSTLING_AGENT_STATE_RUNNING;
    if (std.mem.eql(u8, token, "waiting_input") or
        std.mem.eql(u8, token, "wait_input") or
        std.mem.eql(u8, token, "waiting")) return c.GHOSTLING_AGENT_STATE_WAITING_INPUT;
    if (std.mem.eql(u8, token, "done")) return c.GHOSTLING_AGENT_STATE_DONE;
    if (std.mem.eql(u8, token, "error") or std.mem.eql(u8, token, "failed")) return c.GHOSTLING_AGENT_STATE_ERROR;
    if (std.mem.eql(u8, token, "idle")) return c.GHOSTLING_AGENT_STATE_IDLE;
    return c.GHOSTLING_AGENT_STATE_UNKNOWN;
}

fn parseProtocolPayload(s: *c.GhostlingAgentState, payload: []const u8) void {
    if (payload.len == 0) return;
    var iter = std.mem.splitScalar(u8, payload, '|');
    const ver = iter.next() orelse return;
    if (!std.mem.eql(u8, ver, "v1")) return;
    const state_token = iter.next() orelse return;
    const value = parseStateToken(state_token);
    if (value == c.GHOSTLING_AGENT_STATE_UNKNOWN) return;
    if (iter.next()) |agent| {
        if (agent.len > 0) {
            const len = @min(agent.len, s.protocol_agent.len - 1);
            @memcpy(s.protocol_agent[0..len], agent[0..len]);
            s.protocol_agent[len] = 0;
        }
    }
    updateState(s, value, c.GHOSTLING_AGENT_SOURCE_PROTOCOL, 100);
}

fn oscResetCommand(s: *c.GhostlingAgentState) void {
    s.osc_esc_pending = false;
    s.osc_cmd_decided = false;
    s.osc_collect = true;
    s.osc_cmd = 0;
    s.osc_cmd_digits = 0;
    s.osc_payload_len = 0;
    s.osc_payload[0] = 0;
}

fn oscProcessByte(s: *c.GhostlingAgentState, b: u8) void {
    if (!s.osc_cmd_decided) {
        if (b >= '0' and b <= '9') {
            if (s.osc_cmd <= 99999999) s.osc_cmd = s.osc_cmd * 10 + (b - '0');
            s.osc_cmd_digits += 1;
            return;
        }
        if (b == ';') {
            s.osc_cmd_decided = true;
            s.osc_collect = s.osc_cmd_digits > 0 and s.osc_cmd == 99;
            return;
        }
        s.osc_cmd_decided = true;
        s.osc_collect = false;
        return;
    }
    if (!s.osc_collect) return;
    if (s.osc_payload_len + 1 >= s.osc_payload.len) {
        s.osc_collect = false;
        s.osc_payload_len = 0;
        s.osc_payload[0] = 0;
        return;
    }
    s.osc_payload[s.osc_payload_len] = b;
    s.osc_payload_len += 1;
    s.osc_payload[s.osc_payload_len] = 0;
}

fn oscFinish(s: *c.GhostlingAgentState) void {
    if (s.osc_collect and s.osc_payload_len > 0) {
        const payload = @as([*]u8, @ptrCast(&s.osc_payload))[0..s.osc_payload_len];
        parseProtocolPayload(s, payload);
    }
    s.osc_mode = 0;
    oscResetCommand(s);
}

fn scanProtocolOsc(s: *c.GhostlingAgentState, data: []const u8) void {
    for (data) |b| {
        switch (s.osc_mode) {
            0 => {
                if (b == 0x1B) s.osc_mode = 1;
            },
            1 => {
                if (b == ']') {
                    s.osc_mode = 2;
                    oscResetCommand(s);
                } else if (b == 0x1B) {
                    s.osc_mode = 1;
                } else {
                    s.osc_mode = 0;
                }
            },
            2 => {
                if (s.osc_esc_pending) {
                    s.osc_esc_pending = false;
                    if (b == '\\') {
                        oscFinish(s);
                    } else {
                        oscProcessByte(s, 0x1B);
                        oscProcessByte(s, b);
                    }
                } else if (b == 0x07) {
                    oscFinish(s);
                } else if (b == 0x1B) {
                    s.osc_esc_pending = true;
                } else {
                    oscProcessByte(s, b);
                }
            },
            else => s.osc_mode = 0,
        }
    }
}

fn detectFromTail(s: *c.GhostlingAgentState) void {
    var lowered: [1024]u8 = [_]u8{0} ** 1024;
    const n = @min(s.tail_len, lowered.len - 1);
    for (s.tail[0..n], 0..) |ch, j| {
        lowered[j] = if (ch >= 'A' and ch <= 'Z') ch + 32 else ch;
    }
    lowered[n] = 0;
    const text = lowered[0..n];

    const waiting_needles = [_][]const u8{
        "waiting for input", "awaiting input", "press enter to continue",
        "continue?",         "approve?",       "y/n",
        "[y/n]",
    };
    for (waiting_needles) |needle| {
        if (std.mem.indexOf(u8, text, needle) != null) {
            updateState(s, c.GHOSTLING_AGENT_STATE_WAITING_INPUT, c.GHOSTLING_AGENT_SOURCE_HEURISTIC, 70);
            return;
        }
    }

    const done_needles = [_][]const u8{
        "completed successfully", "all done", "finished", "task complete", "done.",
    };
    for (done_needles) |needle| {
        if (std.mem.indexOf(u8, text, needle) != null) {
            updateState(s, c.GHOSTLING_AGENT_STATE_DONE, c.GHOSTLING_AGENT_SOURCE_HEURISTIC, 65);
            return;
        }
    }

    const error_needles = [_][]const u8{ "error:", "failed", "exception", "traceback" };
    for (error_needles) |needle| {
        if (std.mem.indexOf(u8, text, needle) != null) {
            updateState(s, c.GHOSTLING_AGENT_STATE_ERROR, c.GHOSTLING_AGENT_SOURCE_HEURISTIC, 60);
            return;
        }
    }

    updateState(s, c.GHOSTLING_AGENT_STATE_RUNNING, c.GHOSTLING_AGENT_SOURCE_HEURISTIC, 40);
}

export fn ghostling_agent_state_init(s: *c.GhostlingAgentState) void {
    @memset(@as([*]u8, @ptrCast(s))[0..@sizeOf(c.GhostlingAgentState)], 0);
    s.value = c.GHOSTLING_AGENT_STATE_UNKNOWN;
    s.source = c.GHOSTLING_AGENT_SOURCE_NONE;
    s.confidence = 0;
    s.updated_at_ms = monotonicMs();
    s.osc_mode = 0;
}

export fn ghostling_agent_state_feed_output(s: *c.GhostlingAgentState, data: [*c]const u8, len: usize) void {
    const slice = data[0..len];
    scanProtocolOsc(s, slice);
    appendTail(s, slice);
    detectFromTail(s);
}

export fn ghostling_agent_state_on_local_input(s: *c.GhostlingAgentState) void {
    updateState(s, c.GHOSTLING_AGENT_STATE_RUNNING, c.GHOSTLING_AGENT_SOURCE_LOCAL_INPUT, 90);
}

export fn ghostling_agent_state_on_process_exit(s: *c.GhostlingAgentState, exit_status: i32) void {
    if (exit_status == 0)
        updateState(s, c.GHOSTLING_AGENT_STATE_DONE, c.GHOSTLING_AGENT_SOURCE_PROCESS, 100)
    else
        updateState(s, c.GHOSTLING_AGENT_STATE_ERROR, c.GHOSTLING_AGENT_SOURCE_PROCESS, 100);
}

export fn ghostling_agent_state_label(s: *const c.GhostlingAgentState) [*c]const u8 {
    return switch (s.value) {
        c.GHOSTLING_AGENT_STATE_IDLE => @as([*c]const u8, @ptrCast("idle")),
        c.GHOSTLING_AGENT_STATE_RUNNING => @as([*c]const u8, @ptrCast("running")),
        c.GHOSTLING_AGENT_STATE_WAITING_INPUT => @as([*c]const u8, @ptrCast("waiting")),
        c.GHOSTLING_AGENT_STATE_DONE => @as([*c]const u8, @ptrCast("done")),
        c.GHOSTLING_AGENT_STATE_ERROR => @as([*c]const u8, @ptrCast("error")),
        else => @as([*c]const u8, @ptrCast("unknown")),
    };
}

export fn ghostling_agent_state_agent(s: *const c.GhostlingAgentState) [*c]const u8 {
    if (s.protocol_agent[0] == 0) return @as([*c]const u8, @ptrCast(""));
    return @as([*c]const u8, @ptrCast(&s.protocol_agent));
}

export fn ghostling_agent_state_run_tests() bool {
    var ok = true;
    var s: c.GhostlingAgentState = undefined;
    ghostling_agent_state_init(&s);

    const part1 = [_]u8{ 0x1B, ']', '9', '9', ';', 'v', '1', '|', 'w', 'a' };
    const part2 = [_]u8{ 'i', 't', 'i', 'n', 'g', '_', 'i', 'n', 'p', 'u', 't', '|', 'c', 'o', 'd', 'e', 'x', 0x07 };
    ghostling_agent_state_feed_output(&s, &part1, part1.len);
    ghostling_agent_state_feed_output(&s, &part2, part2.len);
    ok = ok and (s.value == c.GHOSTLING_AGENT_STATE_WAITING_INPUT);

    const done_osc = [_]u8{ 0x1B, ']', '9', '9', ';', 'v', '1', '|', 'd', 'o', 'n', 'e', '|', 'c', 'l', 'a', 'u', 'd', 'e', 0x1B, '\\' };
    ghostling_agent_state_feed_output(&s, &done_osc, done_osc.len);
    ok = ok and (s.value == c.GHOSTLING_AGENT_STATE_DONE);

    ghostling_agent_state_on_local_input(&s);
    ok = ok and (s.value == c.GHOSTLING_AGENT_STATE_RUNNING);

    ghostling_agent_state_on_process_exit(&s, 0);
    ok = ok and (s.value == c.GHOSTLING_AGENT_STATE_DONE);

    return ok;
}
