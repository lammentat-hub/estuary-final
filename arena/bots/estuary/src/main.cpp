#include "planner.hpp"

int main() {
    estuary::Memory memory;
    for(;;) {
        try {
            if(cambc::is_draft()) estuary::draft();
            else estuary::play(memory);
        } catch(const std::exception& e) {
            // Report every unexpected SDK or logic failure; never silently mask
            // an invalid action as normal behaviour. Integration tests reject it.
            std::printf("ESTUARY_ERROR round=%u: %s\n",cambc::round_number(),e.what());
            std::fflush(stdout);
        }
        cambc::end_turn();
    }
}
