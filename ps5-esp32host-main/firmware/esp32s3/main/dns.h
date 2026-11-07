#pragma once
#include <stddef.h>
#include <stdint.h>

// Returns response size, or zero for unsupported/malformed packets.
size_t ps5_dns_reply(const uint8_t *query, size_t size, uint8_t *reply,
                     size_t capacity, const uint8_t ip[4]);
