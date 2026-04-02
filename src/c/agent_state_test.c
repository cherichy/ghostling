#include "agent_state.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool expect_state(const char *name, const GhostlingAgentState *s,
                         GhostlingAgentStateValue want)
{
    if (s->value == want)
        return true;
    fprintf(stderr, "agent_state_test: %s expected=%d got=%d\n", name,
            (int)want, (int)s->value);
    return false;
}

bool ghostling_agent_state_run_tests(void)
{
    bool ok = true;
    GhostlingAgentState s;
    ghostling_agent_state_init(&s);

    static const uint8_t osc_wait_part1[] = {0x1B, ']', '9', '9', ';', 'v', '1', '|', 'w', 'a'};
    static const uint8_t osc_wait_part2[] = {'i', 't', 'i', 'n', 'g', '_', 'i', 'n', 'p', 'u', 't', '|', 'c', 'o', 'd', 'e', 'x', 0x07};
    ghostling_agent_state_feed_output(&s, osc_wait_part1, sizeof(osc_wait_part1));
    ghostling_agent_state_feed_output(&s, osc_wait_part2, sizeof(osc_wait_part2));
    ok &= expect_state("osc99 waiting", &s, GHOSTLING_AGENT_STATE_WAITING_INPUT);
    if (strcmp(ghostling_agent_state_agent(&s), "codex") != 0) {
        fprintf(stderr, "agent_state_test: osc99 agent expected=codex got=%s\n",
                ghostling_agent_state_agent(&s));
        ok = false;
    }

    static const uint8_t osc_done[] = {
        0x1B, ']', '9', '9', ';', 'v', '1', '|', 'd', 'o', 'n', 'e', '|',
        'c', 'l', 'a', 'u', 'd', 'e', 0x1B, '\\'};
    ghostling_agent_state_feed_output(&s, osc_done, sizeof(osc_done));
    ok &= expect_state("osc99 done", &s, GHOSTLING_AGENT_STATE_DONE);

    ghostling_agent_state_on_local_input(&s);
    ok &= expect_state("local input running", &s, GHOSTLING_AGENT_STATE_RUNNING);

    ghostling_agent_state_on_process_exit(&s, 0);
    ok &= expect_state("exit done", &s, GHOSTLING_AGENT_STATE_DONE);

    return ok;
}
