const std = @import("std");

const c = @cImport({
    @cInclude("agent_events.h");
});

export fn agent_event_bus_init(bus: *c.AgentEventBus) void {
    @memset(@as([*]u8, @ptrCast(bus))[0..@sizeOf(c.AgentEventBus)], 0);
    bus.*.next_seq = 1;
}

export fn agent_event_bus_on_state_change(
    userdata: ?*anyopaque,
    tab: ?*const c.struct_Tab,
    before: *const c.GhostlingAgentState,
    after: *const c.GhostlingAgentState,
) void {
    const bus = @as(*c.AgentEventBus, @ptrCast(@alignCast(userdata orelse return)));
    const cap: usize = bus.events.len;
    var idx = (bus.head + bus.len) % cap;
    if (bus.len == cap) {
        idx = bus.head;
        bus.head = (bus.head + 1) % cap;
    } else {
        bus.len += 1;
    }

    bus.events[idx].seq = bus.next_seq;
    bus.events[idx].timestamp_ms = @as(u64, @intCast(std.time.milliTimestamp()));
    bus.events[idx].tab = tab;
    bus.events[idx].before = before.*;
    bus.events[idx].after = after.*;
    bus.next_seq += 1;
}

export fn agent_event_bus_len(bus: *const c.AgentEventBus) usize {
    return bus.len;
}

export fn agent_event_bus_latest(
    bus: *const c.AgentEventBus,
    out_event: *c.GhostlingAgentStateEvent,
) bool {
    if (bus.len == 0) return false;
    const cap: usize = bus.events.len;
    const idx = (bus.head + bus.len - 1) % cap;
    out_event.* = bus.events[idx];
    return true;
}
