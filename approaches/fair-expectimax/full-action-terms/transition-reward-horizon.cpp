// Disables placement priors while retaining the dense transition signals for
// revealed covers and squared continuation-chain depth.  This isolates those
// transition terms inside depth-three search.
#define DROP7_FULL_FAIR_ACTION_PRIORS 0
#define DROP7_FULL_FAIR_SCREEN_SEED_START 0x3ea50000u
#define DROP7_FULL_FAIR_CONFIRMATION_SEED_START 0x3ea60000u
#include "full-fair-horizon.cpp"
