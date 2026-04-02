#include "agent_state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

enum {
    AGENT_OSC_MODE_NORMAL = 0,
    AGENT_OSC_MODE_ESC = 1,
    AGENT_OSC_MODE_OSC = 2,
};

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

static int source_priority(GhostlingAgentStateSource source)
{
    switch (source) {
    case GHOSTLING_AGENT_SOURCE_PROCESS:
        return 50;
    case GHOSTLING_AGENT_SOURCE_PROTOCOL:
        return 40;
    case GHOSTLING_AGENT_SOURCE_LOCAL_INPUT:
        return 30;
    case GHOSTLING_AGENT_SOURCE_HEURISTIC:
        return 10;
    case GHOSTLING_AGENT_SOURCE_NONE:
    default:
        return 0;
    }
}

static bool state_should_override(const GhostlingAgentState *state,
                                  GhostlingAgentStateValue next_value,
                                  GhostlingAgentStateSource next_source)
{
    if (next_source == GHOSTLING_AGENT_SOURCE_LOCAL_INPUT &&
        next_value == GHOSTLING_AGENT_STATE_RUNNING &&
        state->source == GHOSTLING_AGENT_SOURCE_PROTOCOL &&
        state->value != GHOSTLING_AGENT_STATE_RUNNING) {
        return true;
    }

    return source_priority(next_source) >= source_priority(state->source);
}

static void update_state(GhostlingAgentState *state,
                         GhostlingAgentStateValue value,
                         GhostlingAgentStateSource source, uint8_t confidence)
{
    if (!state_should_override(state, value, source))
        return;

    if (state->value == value && state->source == source &&
        state->confidence == confidence)
        return;

    state->value = value;
    state->source = source;
    state->confidence = confidence;
    state->updated_at_ms = monotonic_ms();
}

static void lower_ascii_copy(char *dst, size_t dst_sz, const char *src,
                             size_t src_len)
{
    if (dst_sz == 0)
        return;

    size_t n = src_len;
    if (n + 1 > dst_sz)
        n = dst_sz - 1;

    for (size_t i = 0; i < n; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[n] = '\0';
}

static bool contains_any(const char *text, const char *const *needles,
                         size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (strstr(text, needles[i]))
            return true;
    }
    return false;
}

static void append_tail(GhostlingAgentState *state, const uint8_t *data,
                        size_t len)
{
    if (!data || len == 0)
        return;

    if (len >= sizeof(state->tail) - 1) {
        size_t keep = sizeof(state->tail) - 1;
        memcpy(state->tail, data + (len - keep), keep);
        state->tail[keep] = '\0';
        state->tail_len = keep;
        return;
    }

    size_t cap = sizeof(state->tail) - 1;
    if (state->tail_len + len > cap) {
        size_t overflow = state->tail_len + len - cap;
        memmove(state->tail, state->tail + overflow, state->tail_len - overflow);
        state->tail_len -= overflow;
    }

    memcpy(state->tail + state->tail_len, data, len);
    state->tail_len += len;
    state->tail[state->tail_len] = '\0';
}

static void trim_ascii_spaces(char *s)
{
    if (!s)
        return;

    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && isspace((unsigned char)s[start]))
        start++;
    size_t end = len;
    while (end > start && isspace((unsigned char)s[end - 1]))
        end--;

    if (start > 0)
        memmove(s, s + start, end - start);
    s[end - start] = '\0';
}

static GhostlingAgentStateValue parse_state_token(const char *token)
{
    if (!token)
        return GHOSTLING_AGENT_STATE_UNKNOWN;

    if (strcmp(token, "running") == 0)
        return GHOSTLING_AGENT_STATE_RUNNING;
    if (strcmp(token, "waiting_input") == 0 || strcmp(token, "wait_input") == 0 ||
        strcmp(token, "waiting") == 0)
        return GHOSTLING_AGENT_STATE_WAITING_INPUT;
    if (strcmp(token, "done") == 0)
        return GHOSTLING_AGENT_STATE_DONE;
    if (strcmp(token, "error") == 0 || strcmp(token, "failed") == 0)
        return GHOSTLING_AGENT_STATE_ERROR;
    if (strcmp(token, "idle") == 0)
        return GHOSTLING_AGENT_STATE_IDLE;

    return GHOSTLING_AGENT_STATE_UNKNOWN;
}

static void parse_protocol_payload(GhostlingAgentState *state,
                                   const char *payload)
{
    if (!payload || !payload[0])
        return;

    char local[256];
    snprintf(local, sizeof(local), "%s", payload);

    char *parts[4] = {0};
    size_t n_parts = 0;
    char *cur = local;
    while (n_parts < 4) {
        parts[n_parts++] = cur;
        char *sep = strchr(cur, '|');
        if (!sep)
            break;
        *sep = '\0';
        cur = sep + 1;
    }

    if (n_parts < 2)
        return;

    for (size_t i = 0; i < n_parts; i++)
        trim_ascii_spaces(parts[i]);

    if (strcmp(parts[0], "v1") != 0)
        return;

    GhostlingAgentStateValue value = parse_state_token(parts[1]);
    if (value == GHOSTLING_AGENT_STATE_UNKNOWN)
        return;

    if (n_parts >= 3 && parts[2][0] != '\0') {
        size_t len = strlen(parts[2]);
        if (len >= sizeof(state->protocol_agent))
            len = sizeof(state->protocol_agent) - 1;
        memcpy(state->protocol_agent, parts[2], len);
        state->protocol_agent[len] = '\0';
    }

    update_state(state, value, GHOSTLING_AGENT_SOURCE_PROTOCOL, 100);
}

static void osc_reset_command(GhostlingAgentState *state)
{
    state->osc_esc_pending = false;
    state->osc_cmd_decided = false;
    state->osc_collect = true;
    state->osc_cmd = 0;
    state->osc_cmd_digits = 0;
    state->osc_payload_len = 0;
    state->osc_payload[0] = '\0';
}

static void osc_process_byte(GhostlingAgentState *state, uint8_t b)
{
    if (!state->osc_cmd_decided) {
        if (b >= '0' && b <= '9') {
            if (state->osc_cmd <= 99999999u)
                state->osc_cmd = state->osc_cmd * 10u + (unsigned)(b - '0');
            state->osc_cmd_digits++;
            return;
        }

        if (b == ';') {
            state->osc_cmd_decided = true;
            state->osc_collect =
                (state->osc_cmd_digits > 0 && state->osc_cmd == 99u);
            return;
        }

        state->osc_cmd_decided = true;
        state->osc_collect = false;
        return;
    }

    if (!state->osc_collect)
        return;

    if (state->osc_payload_len + 1 >= sizeof(state->osc_payload)) {
        state->osc_collect = false;
        state->osc_payload_len = 0;
        state->osc_payload[0] = '\0';
        return;
    }

    state->osc_payload[state->osc_payload_len++] = (char)b;
    state->osc_payload[state->osc_payload_len] = '\0';
}

static void osc_finish(GhostlingAgentState *state)
{
    if (state->osc_collect && state->osc_payload_len > 0)
        parse_protocol_payload(state, state->osc_payload);

    state->osc_mode = AGENT_OSC_MODE_NORMAL;
    osc_reset_command(state);
}

static void scan_protocol_osc(GhostlingAgentState *state, const uint8_t *data,
                              size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];

        switch (state->osc_mode) {
        case AGENT_OSC_MODE_NORMAL:
            if (b == 0x1B)
                state->osc_mode = AGENT_OSC_MODE_ESC;
            break;

        case AGENT_OSC_MODE_ESC:
            if (b == ']') {
                state->osc_mode = AGENT_OSC_MODE_OSC;
                osc_reset_command(state);
            } else if (b == 0x1B) {
                state->osc_mode = AGENT_OSC_MODE_ESC;
            } else {
                state->osc_mode = AGENT_OSC_MODE_NORMAL;
            }
            break;

        case AGENT_OSC_MODE_OSC:
            if (state->osc_esc_pending) {
                state->osc_esc_pending = false;
                if (b == '\\') {
                    osc_finish(state);
                } else {
                    osc_process_byte(state, 0x1B);
                    osc_process_byte(state, b);
                }
                break;
            }

            if (b == 0x07) {
                osc_finish(state);
            } else if (b == 0x1B) {
                state->osc_esc_pending = true;
            } else {
                osc_process_byte(state, b);
            }
            break;

        default:
            state->osc_mode = AGENT_OSC_MODE_NORMAL;
            break;
        }
    }
}

static void detect_from_tail(GhostlingAgentState *state)
{
    char lowered[1024];
    lower_ascii_copy(lowered, sizeof(lowered), state->tail, state->tail_len);

    static const char *waiting_needles[] = {
        "waiting for input",
        "awaiting input",
        "press enter to continue",
        "continue?",
        "approve?",
        "y/n",
        "[y/n]",
    };
    if (contains_any(lowered, waiting_needles,
                     sizeof(waiting_needles) / sizeof(waiting_needles[0]))) {
        update_state(state, GHOSTLING_AGENT_STATE_WAITING_INPUT,
                     GHOSTLING_AGENT_SOURCE_HEURISTIC, 70);
        return;
    }

    static const char *done_needles[] = {
        "completed successfully",
        "all done",
        "finished",
        "task complete",
        "done.",
    };
    if (contains_any(lowered, done_needles,
                     sizeof(done_needles) / sizeof(done_needles[0]))) {
        update_state(state, GHOSTLING_AGENT_STATE_DONE,
                     GHOSTLING_AGENT_SOURCE_HEURISTIC, 65);
        return;
    }

    static const char *error_needles[] = {
        "error:",
        "failed",
        "exception",
        "traceback",
    };
    if (contains_any(lowered, error_needles,
                     sizeof(error_needles) / sizeof(error_needles[0]))) {
        update_state(state, GHOSTLING_AGENT_STATE_ERROR,
                     GHOSTLING_AGENT_SOURCE_HEURISTIC, 60);
        return;
    }

    update_state(state, GHOSTLING_AGENT_STATE_RUNNING,
                 GHOSTLING_AGENT_SOURCE_HEURISTIC, 40);
}

void ghostling_agent_state_init(GhostlingAgentState *state)
{
    if (!state)
        return;

    memset(state, 0, sizeof(*state));
    state->value = GHOSTLING_AGENT_STATE_UNKNOWN;
    state->source = GHOSTLING_AGENT_SOURCE_NONE;
    state->confidence = 0;
    state->updated_at_ms = monotonic_ms();
    state->osc_mode = AGENT_OSC_MODE_NORMAL;
}

void ghostling_agent_state_feed_output(GhostlingAgentState *state,
                                       const uint8_t *data, size_t len)
{
    if (!state || !data || len == 0)
        return;

    scan_protocol_osc(state, data, len);
    append_tail(state, data, len);
    detect_from_tail(state);
}

void ghostling_agent_state_on_local_input(GhostlingAgentState *state)
{
    if (!state)
        return;

    update_state(state, GHOSTLING_AGENT_STATE_RUNNING,
                 GHOSTLING_AGENT_SOURCE_LOCAL_INPUT, 90);
}

void ghostling_agent_state_on_process_exit(GhostlingAgentState *state,
                                           int exit_status)
{
    if (!state)
        return;

    if (exit_status == 0)
        update_state(state, GHOSTLING_AGENT_STATE_DONE,
                     GHOSTLING_AGENT_SOURCE_PROCESS, 100);
    else
        update_state(state, GHOSTLING_AGENT_STATE_ERROR,
                     GHOSTLING_AGENT_SOURCE_PROCESS, 100);
}

const char *ghostling_agent_state_label(const GhostlingAgentState *state)
{
    if (!state)
        return "unknown";

    switch (state->value) {
    case GHOSTLING_AGENT_STATE_IDLE:
        return "idle";
    case GHOSTLING_AGENT_STATE_RUNNING:
        return "running";
    case GHOSTLING_AGENT_STATE_WAITING_INPUT:
        return "waiting";
    case GHOSTLING_AGENT_STATE_DONE:
        return "done";
    case GHOSTLING_AGENT_STATE_ERROR:
        return "error";
    case GHOSTLING_AGENT_STATE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *ghostling_agent_state_agent(const GhostlingAgentState *state)
{
    if (!state || state->protocol_agent[0] == '\0')
        return "";
    return state->protocol_agent;
}
