// Captive DNS (FSD FR-13.5), pure answer core. No ESP-IDF headers.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Build the reply to one DNS query: an A/IN question gets `ip` (TTL 60 s), any
// other question gets an empty NOERROR reply. Returns the reply length, or 0 for
// a packet that is not a single well-formed question (it is then dropped).
int dc_answer(const uint8_t *q, size_t qlen, uint8_t *out, size_t cap, const uint8_t ip[4]);
