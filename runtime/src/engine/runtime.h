#pragma once

namespace ltm {

// Called on the game's main thread right after il2cpp_init returned.
void runtime_start(void *self_module, void *game_assembly);

}  // namespace ltm
