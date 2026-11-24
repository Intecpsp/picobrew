/* Included in the test copy of the generated writer, with original/packed pairs. */
void test_literal_bounds(void)
{
    ZymaticNetwork expected, actual;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (unsigned start = 0; start <= sizeof(actual.buffer.reply) + 1; ++start) {
            memset(&expected, 0x5a, sizeof(expected));
            expected.response_bytes = start;
            memcpy(&actual, &expected, sizeof(actual));
            for (const char *p = cases[i][0]; *p; ++p) append(&expected, *p);
            literal(&actual, CONSTANT(cases[i][1]));
            assert(!memcmp(&actual, &expected, sizeof(actual)));
        }
    }
}
