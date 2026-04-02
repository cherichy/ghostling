#include "agent_events.h"

#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

static uint64_t monotonic_ms(void)
{
#if defined(_WIN32)
    return GetTickCount64();
#else
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

void agent_event_bus_init(AgentEventBus *bus)
{
    if (!bus)
        return;
    memset(bus, 0, sizeof(*bus));
    bus->next_seq = 1;
}

void agent_event_bus_on_state_change(void *userdata, const struct Tab *tab,
                                     const GhostlingAgentState *before,
                                     const GhostlingAgentState *after)
{
    AgentEventBus *bus = (AgentEventBus *)userdata;
    if (!bus || !before || !after)
        return;

    size_t idx = (bus->head + bus->len) % (sizeof(bus->events) / sizeof(bus->events[0]));
    if (bus->len == (sizeof(bus->events) / sizeof(bus->events[0]))) {
        idx = bus->head;
        bus->head = (bus->head + 1) % (sizeof(bus->events) / sizeof(bus->events[0]));
    } else {
        bus->len++;
    }

    bus->events[idx].seq = bus->next_seq++;
    bus->events[idx].timestamp_ms = monotonic_ms();
    bus->events[idx].tab = tab;
    bus->events[idx].before = *before;
    bus->events[idx].after = *after;
}

size_t agent_event_bus_len(const AgentEventBus *bus)
{
    return bus ? bus->len : 0;
}

bool agent_event_bus_latest(const AgentEventBus *bus,
                            GhostlingAgentStateEvent *out_event)
{
    if (!bus || !out_event || bus->len == 0)
        return false;

    size_t idx = (bus->head + bus->len - 1) %
                 (sizeof(bus->events) / sizeof(bus->events[0]));
    *out_event = bus->events[idx];
    return true;
}
