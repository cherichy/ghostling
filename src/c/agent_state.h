#ifndef GHOSTLING_AGENT_STATE_H
#define GHOSTLING_AGENT_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    GHOSTLING_AGENT_STATE_UNKNOWN = 0,
    GHOSTLING_AGENT_STATE_IDLE,
    GHOSTLING_AGENT_STATE_RUNNING,
    GHOSTLING_AGENT_STATE_WAITING_INPUT,
    GHOSTLING_AGENT_STATE_DONE,
    GHOSTLING_AGENT_STATE_ERROR,
} GhostlingAgentStateValue;

typedef enum {
    GHOSTLING_AGENT_SOURCE_NONE = 0,
    GHOSTLING_AGENT_SOURCE_PROTOCOL,
    GHOSTLING_AGENT_SOURCE_HEURISTIC,
    GHOSTLING_AGENT_SOURCE_LOCAL_INPUT,
    GHOSTLING_AGENT_SOURCE_PROCESS,
} GhostlingAgentStateSource;

typedef struct {
    GhostlingAgentStateValue value;
    GhostlingAgentStateSource source;
    uint8_t confidence;
    uint64_t updated_at_ms;
    char protocol_agent[32];

    int osc_mode;
    bool osc_esc_pending;
    bool osc_cmd_decided;
    bool osc_collect;
    unsigned osc_cmd;
    size_t osc_cmd_digits;
    char osc_payload[256];
    size_t osc_payload_len;

    char tail[1024];
    size_t tail_len;
} GhostlingAgentState;

void ghostling_agent_state_init(GhostlingAgentState *state);
void ghostling_agent_state_feed_output(GhostlingAgentState *state,
                                       const uint8_t *data, size_t len);
void ghostling_agent_state_on_local_input(GhostlingAgentState *state);
void ghostling_agent_state_on_process_exit(GhostlingAgentState *state,
                                           int exit_status);
const char *ghostling_agent_state_label(const GhostlingAgentState *state);
const char *ghostling_agent_state_agent(const GhostlingAgentState *state);

#endif
