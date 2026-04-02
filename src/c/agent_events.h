#ifndef GHOSTLING_AGENT_EVENTS_H
#define GHOSTLING_AGENT_EVENTS_H

#include "agent_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Tab;

typedef struct {
    uint64_t seq;
    uint64_t timestamp_ms;
    const struct Tab *tab;
    GhostlingAgentState before;
    GhostlingAgentState after;
} GhostlingAgentStateEvent;

typedef struct {
    GhostlingAgentStateEvent events[64];
    size_t head;
    size_t len;
    uint64_t next_seq;
} AgentEventBus;

void agent_event_bus_init(AgentEventBus *bus);
void agent_event_bus_on_state_change(void *userdata, const struct Tab *tab,
                                     const GhostlingAgentState *before,
                                     const GhostlingAgentState *after);
size_t agent_event_bus_len(const AgentEventBus *bus);
bool agent_event_bus_latest(const AgentEventBus *bus,
                            GhostlingAgentStateEvent *out_event);

#endif
