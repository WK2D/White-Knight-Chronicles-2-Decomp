#ifndef WKC_PLAY_ENVIRONMENT_H
#define WKC_PLAY_ENVIRONMENT_H
namespace wkc {
// WKC2_PLAYER_MODE=1: clear inherited research state before runtime startup.
// Leaves platform environment and the explicit play/media allowlist intact.
void prepare_play_environment();
bool play_environment_is_clean();
}
#endif
