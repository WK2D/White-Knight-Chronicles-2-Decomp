#include "spu_helpers.h"
#include <stdint.h>
#include <stdio.h>

/* Fixed eight-bit reference, independent of the production implementation. */
static u128 count_bits_reference(u128 a)
{
    u128 result;
    for (unsigned byte = 0; byte < 16; ++byte) {
        unsigned count = 0;
        for (unsigned bit = 0; bit < 8; ++bit)
            count += ((unsigned)a._u8[byte] >> bit) & 1u;
        result._u8[byte] = (uint8_t)count;
    }
    return result;
}

static uint32_t random_word(uint32_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void check_vector(u128 input, unsigned* compared, unsigned* failures)
{
    u128 expected = count_bits_reference(input);
    u128 actual = spu_cntb(input);
    for (unsigned byte = 0; byte < 16; ++byte) {
        ++*compared;
        if (actual._u8[byte] != expected._u8[byte]) {
            if ((*failures)++ < 8)
                fprintf(stderr,
                        "CNTB byte %u input=%02X got=%u expected=%u\n",
                        byte, (unsigned)input._u8[byte],
                        (unsigned)actual._u8[byte],
                        (unsigned)expected._u8[byte]);
        }
    }
}

int main(void)
{
    uint32_t state = 0x2AC35D7Eu;
    unsigned compared = 0, failures = 0;

    /* Every possible byte value occurs at every host position, with distinct
     * random neighbours to catch accidental shifts across byte boundaries. */
    for (unsigned value = 0; value < 256; ++value) {
        for (unsigned position = 0; position < 16; ++position) {
            u128 input;
            for (unsigned word = 0; word < 4; ++word)
                input._u32[word] = random_word(&state);
            input._u8[position] = (uint8_t)value;
            check_vector(input, &compared, &failures);
        }
    }
    for (unsigned vector = 0; vector < 65536; ++vector) {
        u128 input;
        for (unsigned word = 0; word < 4; ++word)
            input._u32[word] = random_word(&state);
        check_vector(input, &compared, &failures);
    }

    printf("SPU CNTB: %u byte comparisons; %u mismatches\n",
           compared, failures);
    return failures ? 1 : 0;
}
