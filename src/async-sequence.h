/*
 * Aidan Shafran
 * MIT License
 */

/*
 * Begins an asynchronous sequence function. This should be
 * the first call of the function. Pass in the userdata and
 * declare any data that should be available throughout the
 * function. Creates a struct called seqdata for storage.
 * See ASYNC_SEQ_WAIT and ASYNC_SEQ_END.
 *
 * Example usage:
 * gboolean async_seq(void *userdata) {
 *     ASYNC_SEQ_BEGIN(userdata, )
 *     g_message("0 seconds have passed");
 *     g_timeout_add_seconds(1, async_seq, seqdata);
 *     ASYNC_SEQ_WAIT(1, FALSE)
 *     g_message("1 second has passed");
 *     g_timeout_add_seconds(1, async_seq, seqdata);
 *     ASYNC_SEQ_WAIT(2, FALSE)
 *     g_message("2 seconds have passed. complete.");
 *     ASYNC_SEQ_END(FALSE)
 * }
 *
 * The initial caller of async_seq will see the function
 * return immediately after priting the first message, but
 * it will continue to "run" like a regular function until
 * all three messages are printed.
 */
#define ASYNC_SEQ_BEGIN(ud, storage) \
	struct _seqdata_t { guint _seqindex; storage }; \
	struct _seqdata_t *seqdata = ud ? ud : g_new0(struct _seqdata_t, 1); \
	switch(seqdata->_seqindex) { case 0: { \
		++seqdata->_seqindex;

/*
 * Call this after each async operation that should be
 * waited upon. The async option should be set to call your
 * asynchronous sequence function when complete, and pass
 * the seqdata as userdata. Call with singularly increasing
 * values of seqindex starting at 1.
 */
#define ASYNC_SEQ_WAIT(seqindex, ret) \
		return ret; \
	} case seqindex: { \
		++seqdata->_seqindex;

/*
 * Call this as the last call in the function. It ends the
 * asynchronous sequence.
 */
#define ASYNC_SEQ_END(ret) \
	break; \
	} default: { \
		g_warning("async sequence in %s reached default case (value %i)", \
			__FUNCTION__, seqdata->_seqindex); break; \
	} } \
	g_free(seqdata); return ret; 

