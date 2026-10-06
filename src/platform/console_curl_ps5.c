/*
 * PSXS5 - pulls in the boilerplate's console_curl helpers for the PS5 build only.
 */
#if defined(__PROSPERO__) && defined(PSXS5_HAVE_CURL)
#include "../../examples/update-check/console_curl.c"
#endif
