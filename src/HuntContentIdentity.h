#ifndef MOD_NATIVE_HUNTS_CONTENT_IDENTITY_H
#define MOD_NATIVE_HUNTS_CONTENT_IDENTITY_H
namespace hunts::content
{
// Persistent Content Manager ownership key, NOT the module loader identity.
// Preserve existing allocations/owned rows until CM supports explicit adoption.
// See NATIVE_HUNTS_CLEANUP.md. Do not probe a second package as a fallback.
inline constexpr char Package[] = "mod-hunts";
}
#endif
