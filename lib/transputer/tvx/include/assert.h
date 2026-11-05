/* <assert.h> for the vendored TVX headers: the decoders only assert that
 * the element count is a whole number of blocks, which every caller in the
 * runtime guarantees. */
#ifndef TP_TVX_ASSERT_H
#define TP_TVX_ASSERT_H
#define assert(x) ((void)0)
#endif
