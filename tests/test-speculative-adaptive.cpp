#include "speculative-adaptive.h"

#undef NDEBUG

#include <cassert>
#include <cstdio>

static void test_reset(void) {
    common_speculative_adaptive ctrl;

    // cold start at the floor max(1, n_min_adaptive)
    ctrl.reset(8, 1);
    assert(ctrl.n_cur == 1);
    assert(ctrl.n_climb == 0);
    assert(ctrl.n_drop == 0);

    // the default adaptive floor of 3 starts the controller at depth 3
    ctrl.reset(8, 3);
    assert(ctrl.n_cur == 3);

    // the ceiling clamps the cold start to n_max
    ctrl.reset(1, 3);
    assert(ctrl.n_cur == 1);
}

static void test_climb(void) {
    common_speculative_adaptive ctrl;
    ctrl.reset(8, 1); // ceiling 8, cold start at the floor 1

    // depth 1 climbs after 2 consecutive full accepts
    ctrl.update(1, 1, 8, 1);
    assert(ctrl.n_cur == 1);
    ctrl.update(1, 1, 8, 1);
    assert(ctrl.n_cur == 2);

    // a miss resets the climb streak
    ctrl.update(2, 1, 8, 1); // near miss
    assert(ctrl.n_cur == 2);
    assert(ctrl.n_climb == 0);

    // depth 2 climbs after 4 consecutive full accepts
    for (int i = 0; i < 3; ++i) {
        ctrl.update(2, 2, 8, 1);
        assert(ctrl.n_cur == 2);
    }
    ctrl.update(2, 2, 8, 1);
    assert(ctrl.n_cur == 3);

    // depth 3 is the hardened barrier: 10 consecutive full accepts to reach
    // depth 4, so prose/reasoning stay pinned at the floor
    for (int i = 0; i < 9; ++i) {
        ctrl.update(3, 3, 8, 1);
        assert(ctrl.n_cur == 3);
    }
    ctrl.update(3, 3, 8, 1);
    assert(ctrl.n_cur == 4);

    // a full accept of a draft truncated below the depth (e.g. clamped by the
    // server context bound) counts as a full accept, not as a miss
    ctrl.update(3, 3, 8, 1); // depth 4, only 3 tokens drafted, all accepted
    assert(ctrl.n_climb == 1);
    assert(ctrl.n_drop == 0);

    // depth 4 needs 6 consecutive full accepts
    for (int i = 0; i < 5; ++i) {
        ctrl.update(4, 4, 8, 1);
    }
    assert(ctrl.n_cur == 5);

    // depth 5 needs 3 consecutive full accepts
    for (int i = 0; i < 2; ++i) {
        ctrl.update(5, 5, 8, 1);
        assert(ctrl.n_cur == 5);
    }
    ctrl.update(5, 5, 8, 1);
    assert(ctrl.n_cur == 6);

    // depth 6 needs 2 consecutive full accepts
    ctrl.update(6, 6, 8, 1);
    assert(ctrl.n_cur == 6);
    ctrl.update(6, 6, 8, 1);
    assert(ctrl.n_cur == 7);

    // depth 7+ needs 2 consecutive full accepts
    ctrl.update(7, 7, 8, 1);
    assert(ctrl.n_cur == 7);
    ctrl.update(7, 7, 8, 1);
    assert(ctrl.n_cur == 8);

    // the ceiling blocks further climbs
    for (int i = 0; i < 8; ++i) {
        ctrl.update(8, 8, 8, 1);
    }
    assert(ctrl.n_cur == 8);

    // no feedback for a zero-length draft
    ctrl.update(0, 0, 8, 1);
    assert(ctrl.n_cur == 8);
    assert(ctrl.n_climb == 0);
}

static void test_drop(void) {
    common_speculative_adaptive ctrl;
    ctrl.reset(8, 1); // cold start at the floor

    // at the floor no pressure accumulates at all
    for (int i = 0; i < 100; ++i) {
        ctrl.update(1, 0, 8, 1);
    }
    assert(ctrl.n_cur == 1);
    assert(ctrl.n_drop == 0);

    // climb to depth 3 (2 + 4 full accepts)
    for (int i = 0; i < 2; ++i) {
        ctrl.update(1, 1, 8, 1);
    }
    for (int i = 0; i < 4; ++i) {
        ctrl.update(2, 2, 8, 1);
    }
    assert(ctrl.n_cur == 3);

    // depth 3 has a budget of 6; each total miss adds 3
    ctrl.update(3, 0, 8, 1);
    assert(ctrl.n_cur == 3);
    assert(ctrl.n_drop == 3);
    ctrl.update(3, 0, 8, 1);
    assert(ctrl.n_cur == 2);
    assert(ctrl.n_drop == 0);

    // depth 2 has a budget of 4; four near misses drop one step
    for (int i = 0; i < 3; ++i) {
        ctrl.update(2, 1, 8, 1);
        assert(ctrl.n_cur == 2);
    }
    assert(ctrl.n_drop == 3);
    ctrl.update(2, 1, 8, 1);
    assert(ctrl.n_cur == 1);
    assert(ctrl.n_drop == 0);

    // back at the floor, misses no longer accumulate pressure
    for (int i = 0; i < 100; ++i) {
        ctrl.update(1, 0, 8, 1);
    }
    assert(ctrl.n_cur == 1);
    assert(ctrl.n_drop == 0);

    // at depth 5 the budget is 10; a total miss adds 5, so two drop it
    ctrl.reset(8, 1);
    ctrl.n_cur = 5; // simulate a controller that already climbed to 5
    ctrl.update(5, 0, 8, 1);
    assert(ctrl.n_cur == 5);
    assert(ctrl.n_drop == 5);
    ctrl.update(5, 0, 8, 1); // 5 + 5 >= 8 -> drop
    assert(ctrl.n_cur == 4);
    assert(ctrl.n_drop == 0);

    assert(common_speculative_adaptive::drop_pressure(12) == 24);
}

static void test_half_climb_accept_resets_pressure(void) {
    common_speculative_adaptive ctrl;
    ctrl.reset(8, 1);
    ctrl.n_cur = 3;

    // at depth 3, the 10-round climb threshold clears pressure at five full accepts
    for (int i = 0; i < 4; ++i) {
        ctrl.update(3, 2, 8, 1); // near miss: +1 pressure at depth 3
    }
    assert(ctrl.n_drop == 4);
    for (int i = 0; i < 4; ++i) {
        ctrl.update(3, 3, 8, 1);
        assert(ctrl.n_drop == 4);
        assert(ctrl.n_cur == 3);
    }
    ctrl.update(3, 3, 8, 1);
    assert(ctrl.n_drop == 0);
    assert(ctrl.n_cur == 3);
    assert(ctrl.n_climb == 5);

    // the miss pressure uses the drafted count, not the depth: a truncated
    // draft (2 tokens at depth 3) with 1 accepted adds 1, not 2
    ctrl.update(2, 1, 8, 1);
    assert(ctrl.n_drop == 1);

    // an odd climb threshold of 3 clears pressure after ceil(3 / 2) = 2 accepts
    ctrl.reset(8, 1);
    ctrl.n_cur = 5;
    ctrl.n_drop = 7;
    ctrl.update(5, 5, 8, 1);
    assert(ctrl.n_drop == 7);
    ctrl.update(5, 5, 8, 1);
    assert(ctrl.n_drop == 0);
    assert(ctrl.n_cur == 5);
    ctrl.update(5, 5, 8, 1);
    assert(ctrl.n_cur == 6);

    // at depth 12, one of the two full accepts needed to climb clears 14 pressure
    ctrl.reset(12, 3);
    ctrl.n_cur = 12;
    ctrl.n_drop = 14;
    ctrl.update(12, 12, 12, 3);
    assert(ctrl.n_cur == 12);
    assert(ctrl.n_drop == 0);
    assert(ctrl.n_climb == 1);

    // the depth-12 drop budget is 24
    ctrl.n_drop = 22;
    ctrl.update(12, 11, 12, 3);
    assert(ctrl.n_cur == 12);
    assert(ctrl.n_drop == 23);
    ctrl.update(12, 11, 12, 3);
    assert(ctrl.n_cur == 11);
    assert(ctrl.n_drop == 0);
}

static void test_floor(void) {
    common_speculative_adaptive ctrl;

    // with the floor at 2 the depth never drops below 2, no matter how bad
    // the content gets
    ctrl.reset(8, 2);
    for (int i = 0; i < 1000; ++i) {
        ctrl.update(2, 0, 8, 2);
    }
    assert(ctrl.n_cur == 2);
    assert(ctrl.n_drop == 0);

    // climbs still work from the floor
    for (int i = 0; i < 4; ++i) {
        ctrl.update(2, 2, 8, 2);
    }
    assert(ctrl.n_cur == 3);

    // and drops stop at the floor, not below it
    for (int i = 0; i < 100; ++i) {
        ctrl.update(3, 0, 8, 2);
    }
    assert(ctrl.n_cur == 2);
}

int main(void) {
    test_reset();
    test_climb();
    test_drop();
    test_half_climb_accept_resets_pressure();
    test_floor();

    printf("test-speculative-adaptive: all tests OK\n\n");

    return 0;
}
